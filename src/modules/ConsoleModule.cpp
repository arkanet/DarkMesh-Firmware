#include "ConsoleModule.h"

#include <compression/unishox2.h>

#include "SOSBuzzModule.h"
#include "MeshService.h"
#include "MeshModule.h"
#include "Telemetry/EnvironmentTelemetry.h"
#include "Telemetry/PowerTelemetry.h"
#include "Telemetry/DeviceTelemetry.h"
#include "NodeDB.h"
#include "configuration.h"
#include "graphics/Screen.h"

ConsoleModule *consoleModule;

void ConsoleModule::sendText(
    const NodeNum dest,
    const ChannelIndex channel,
    const char *message,
    bool wantReplies,
    const bool compressed) {

    meshtastic_MeshPacket *p = router->allocForSending();

    boolean compressNotWorth = false;

    p->decoded.payload.size = strlen(message);
    p->to = dest;
    p->channel = channel;
    p->want_ack = true;

    if (compressed) {
        p->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_COMPRESSED_APP;
        const size_t payload_len = p->decoded.payload.size;

        // sanity check
        if (payload_len == 0 || payload_len > meshtastic_Constants_DATA_PAYLOAD_LEN) {
            LOG_WARN("Invalid payload length for compression: %u", (unsigned)payload_len);
            p->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
            return;
        }

        uint8_t original_payload[meshtastic_Constants_DATA_PAYLOAD_LEN] = {0};
        uint8_t compressed_out[meshtastic_Constants_DATA_PAYLOAD_LEN] = {0};

        memcpy(original_payload, message, payload_len);

        int compressed_len = unishox2_compress_simple(
            (const char *)original_payload,
            (int)payload_len,
            (char *)compressed_out
        );

        LOG_DEBUG("Original length - %u", (unsigned)payload_len);
        LOG_DEBUG("Compressed length - %d", compressed_len);
        LOG_DEBUG("Original message - %.*s", (int)payload_len, (const char *)original_payload);

        // compression failed or not worth it
        if (compressed_len <= 0 || compressed_len >= (int)payload_len) {
            LOG_DEBUG("Not using compressed message");
            compressNotWorth = true;
        } else {
            LOG_DEBUG("Using compressed message");

            memcpy(p->decoded.payload.bytes, compressed_out, (size_t)compressed_len);
            p->decoded.payload.size = (size_t)compressed_len;
            p->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_COMPRESSED_APP;
        }
    }

    if (!compressed || compressNotWorth) {
        p->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
        memcpy(p->decoded.payload.bytes, message, p->decoded.payload.size);
    }

    LOG_INFO("Send message id=%d, dest=%x, msg=%.*s", p->id, p->to, p->decoded.payload.size, p->decoded.payload.bytes);

    service->sendToMesh(
        p, RX_SRC_LOCAL,
        true); // send to mesh, cc to phone. Even if there's no phone connected, this stores the message to match ACKs
}


ProcessMessage ConsoleModule::handleReceived(const meshtastic_MeshPacket &mp) {
#if defined(DEBUG_PORT) && !defined(DEBUG_MUTE)
    auto &p = mp.decoded;
    LOG_INFO("Console module: text msg from=0x%0x, id=0x%x, channel=%d,  msg=%.*s", mp.from, mp.id, mp.channel,
             p.payload.size, p.payload.bytes);
#endif
    // se il messaggio proviene da una delle chiavi autorizzate,
    // si prosegue

    const bool authorized =
            (config.security.admin_key[0].size == 32 &&
             memcmp(mp.public_key.bytes, config.security.admin_key[0].bytes, 32) == 0) ||
            (config.security.admin_key[1].size == 32 &&
             memcmp(mp.public_key.bytes, config.security.admin_key[1].bytes, 32) == 0) ||
            (config.security.admin_key[2].size == 32 &&
             memcmp(mp.public_key.bytes, config.security.admin_key[2].bytes, 32) == 0);

    if (!authorized) {
        LOG_INFO("message from no-authorized keys");
        return ProcessMessage::CONTINUE;
    }

    if (strncmp(reinterpret_cast<const char *>(p.payload.bytes), "+++", 3) == 0) {
        command_state = !command_state;
        auto msg = vformat("Cmd state: %s", command_state ? "on" : "off");
        sendText(mp.from, 0, msg.c_str(), false);
    }

    if (!command_state) return ProcessMessage::CONTINUE;

    bool useCompression = false;

    struct PayloadView {
        const pb_byte_t *bytes;
        pb_size_t size;
    };

    PayloadView rxPayload {
        p.payload.bytes,
        p.payload.size
    };

    if (rxPayload.size > 2 &&
        rxPayload.bytes[0] == static_cast<pb_byte_t>('C') &&
        rxPayload.bytes[1] == static_cast<pb_byte_t>(':')) {

        useCompression = true;

        rxPayload.bytes += 2;
        rxPayload.size  -= 2;
    }

    if (rxPayload.size > 0 and rxPayload.bytes[0] == 'H') {
        sendText(mp.from, 0, "Hello from firmware!", false, useCompression);
    } else if (rxPayload.size > 0 and rxPayload.bytes[0] == 'N') {
        LOG_INFO("Nodes");
        // 0 = ourself
        uint16_t max_nodes = 4;
        std::string route = "Nodes:\n";
        for (size_t i = 1; i < nodeDatabase.nodes.size(); i++) {
            const auto &entry = nodeDatabase.nodes[i];
            if (entry.hops_away == 0 && entry.num != 0 && entry.snr != 0.0) {
                route += vformat("%s: '%s' snr: %.2f\n", entry.user.short_name, entry.user.long_name, entry.snr);
                if (!--max_nodes) break;
            }
        }
        sendText(mp.from, 0, route.c_str(), false, useCompression);
    } else if (rxPayload.size > 0 and rxPayload.bytes[0] == 'T') {
        // telemetry
        meshtastic_Telemetry m = meshtastic_Telemetry_init_zero;
        m.which_variant = meshtastic_Telemetry_environment_metrics_tag;
        // check if module is loaded
        auto *environment_telemetry_module = (EnvironmentTelemetryModule *) MeshModule::getModule(
            "EnvironmentTelemetry");
        if (!environment_telemetry_module) {
            sendText(mp.from, 0, "environment telemetry not enabled!", false, useCompression);
            return ProcessMessage::CONTINUE;
        }
        if (environment_telemetry_module->extGetEnvironmentTelemetry(&m)) {
            std::string msg = "Environment:\n";
            if (m.variant.environment_metrics.has_temperature) {
                msg += vformat(" T : %f\n", m.variant.environment_metrics.temperature);
            }
            if (m.variant.environment_metrics.has_relative_humidity) {
                msg += vformat("RH: %f\n", m.variant.environment_metrics.relative_humidity);
            }
            if (m.variant.environment_metrics.has_barometric_pressure) {
                msg += vformat(" P: %f\n", m.variant.environment_metrics.barometric_pressure);
            }
            sendText(mp.from, 0, msg.c_str(), false, useCompression);
        }
        auto *power_telemetry_module = (PowerTelemetryModule *) MeshModule::getModule("PowerTelemetry");
        if (!power_telemetry_module) {
            sendText(mp.from, 0, "power telemetry not enabled!", false, useCompression);
            return ProcessMessage::CONTINUE;
        }
        m = meshtastic_Telemetry_init_zero;
        m.which_variant = meshtastic_Telemetry_power_metrics_tag;
        if (power_telemetry_module->extGetPowerTelemetry(&m)) {
            std::string msg = "Power:\n";
            if (m.variant.power_metrics.has_ch3_voltage) {
                msg += vformat("V3 : %f\n", m.variant.power_metrics.ch3_voltage);
            }
            if (m.variant.power_metrics.has_ch3_current) {
                msg += vformat("I3: %f\n", m.variant.power_metrics.ch3_current);
            }
            sendText(mp.from, 0, msg.c_str(), false, useCompression);
        }
    } else if (rxPayload.size > 0 and rxPayload.bytes[0] == 'C') {
        // nodi preferiti C? = lista, C+<id>, aggiunge, C-<id> toglie
        LOG_INFO("Favorites");
        if (rxPayload.size == 1) {
            // only C = list
            std::string msg = "Favorites:\n";
            int fav = 0;
            for (size_t i = 1; i < nodeDatabase.nodes.size(); i++) {
                const auto &entry = nodeDatabase.nodes[i];
                if (entry.is_favorite) {
                    msg += vformat("!%08x: '%s'\n", entry.num, entry.user.short_name);
                    fav++;
                }
                if (fav == 5) {
                    sendText(mp.from, 0, msg.c_str(), false, useCompression);
                    fav = 0;
                    msg = "Favorites:\n";
                }
            }
            if (fav > 0) {
                sendText(mp.from, 0, msg.c_str(), false, useCompression);
            }
        } else if (rxPayload.size == 10) {
            // C+<hex id>
            // parse hex id
            auto new_favorite_id = static_cast<uint32_t>(strtoul(reinterpret_cast<const char *>(rxPayload.bytes + 2),
                                                                 nullptr, 16));
            bool set = (rxPayload.bytes[1] == '+');
            if (set || rxPayload.bytes[1] == '-') {
                nodeDB->set_favorite(set, new_favorite_id);
                LOG_INFO("%s %d as favorite", set ? "Setting" : "Unsetting", new_favorite_id);
                auto msg = vformat("%d %s as favorite", new_favorite_id, set ? "set" : "unset");
                sendText(mp.from, 0, msg.c_str(), false, useCompression);
            }
        }
    } else if (rxPayload.size > 0 and rxPayload.bytes[0] == 'B') {
        // blacklist
        if (rxPayload.size == 1) {
            // list
            std::string msg = "Blacklist:\n";
            for (size_t i = 1; i < nodeDatabase.nodes.size(); i++) {
                const auto &entry = nodeDatabase.nodes[i];
                if (entry.is_ignored) {
                    msg += vformat("!%08x: '%s'\n", entry.num, entry.user.short_name);
                }
            }
            sendText(mp.from, 0, msg.c_str(), false, useCompression);
        } else if (rxPayload.size == 10) {
            auto new_bl_id = static_cast<uint32_t>(strtoul(reinterpret_cast<const char *>(rxPayload.bytes + 2), nullptr,
                                                           16));
            meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(new_bl_id);
            if (node == nullptr) {
                sendText(mp.from, 0, "node not found in db", false);
                return ProcessMessage::CONTINUE;
            }
            bool set = (rxPayload.bytes[1] == '+');
            if (set || rxPayload.bytes[1] == '-') {
                node->is_ignored = set;
                nodeDB->saveToDisk(SEGMENT_NODEDATABASE);
                // salviamo il nodedb
                auto msg = vformat("%u %sblacklisted", new_bl_id, set ? "" : "not ");
                sendText(mp.from, 0, msg.c_str(), false, useCompression);
            }
        }
    } else if (rxPayload.size > 0 and rxPayload.bytes[0] == 'S') {
        //voltage and battery stats

        std::string msg = "Device Stats:\n";

        if (rxPayload.size == 1) {
            //raw method to obtain battery stats
            if (!power) {
                sendText(mp.from, 0, "Power manager not ready", false, useCompression);
                return ProcessMessage::CONTINUE;
            }

            uint16_t voltage = Power::getLastVoltageRead();
            uint8_t battPercent = Power::getLastBattPercentRead();

            //FIXME these params seem to be not evaluated correctly
            //eg. when the device is charging, it does not show as charging

            bool charging = Power::isBatteryCharging();
            //bool usbPowered = power->isUsbPowered();
            //bool batteryConnected = power->isBatteryConnect();

            msg += vformat("VLT: %u mV\n", voltage);
            msg += vformat("PRC: %u%%\n", battPercent);

            msg += vformat("CHG: %s\n", charging ? "Y" : "N");
            //msg += vformat("USB PWR: %s\n", usbPowered ? "Y" : "N");
            //msg += vformat("BATT CONN: %s\n", batteryConnected ? "Y" : "N");

        } else if (rxPayload.size >= 2 and rxPayload.bytes[1] == '+') {
            //enumerates direct nodes detected
            auto deviceTelemetry = (DeviceTelemetryModule *) MeshModule::getModule("DeviceTelemetry");

            if (deviceTelemetry != nullptr) {
                meshtastic_Telemetry devTel = deviceTelemetry->extGetDeviceTelemetry();
                meshtastic_Telemetry localStats = deviceTelemetry->extGetDeviceLocalStats();

                if (rxPayload.size == 2 && devTel.which_variant == meshtastic_Telemetry_device_metrics_tag) {

                    msg += vformat("Uptime: %u\n",
                                  devTel.variant.device_metrics.uptime_seconds);

                    msg += vformat("Voltage: %.2f\n",
                                   devTel.variant.device_metrics.voltage);

                    msg += vformat("Battery: %u\n",
                                   devTel.variant.device_metrics.battery_level);

                    msg += vformat("AirUtilTX: %.2f\n",
                                   devTel.variant.device_metrics.air_util_tx);

                    msg += vformat("CH-Util: %.1f\n",
                                   devTel.variant.device_metrics.channel_utilization);

                } else if (localStats.which_variant == meshtastic_Telemetry_local_stats_tag){

                    msg += vformat("Online nodes: %u\n",
                                                   localStats.variant.local_stats.num_online_nodes);

                    msg += vformat("Total nodes: %u\n",
                                   localStats.variant.local_stats.num_total_nodes);

                    msg += vformat("Packets TX: %u\n",
                                   localStats.variant.local_stats.num_packets_tx);

                    msg += vformat("Packets RX: %u\n",
                                   localStats.variant.local_stats.num_packets_rx);

                    msg += vformat("Packets RX bad: %u\n",
                                   localStats.variant.local_stats.num_packets_rx_bad);

                    msg += vformat("TX relay: %u\n",
                                   localStats.variant.local_stats.num_tx_relay);

                    msg += vformat("TX dropped: %u\n",
                                   localStats.variant.local_stats.num_tx_dropped);
#ifndef ARCH_PORTDUINO
                    msg += vformat("Heap total: %u B\n",
                                   localStats.variant.local_stats.heap_total_bytes);

                    msg += vformat("Heap free: %u B\n",
                                   localStats.variant.local_stats.heap_free_bytes);
#endif
                }
            }
        }

        sendText(mp.from, 0, msg.c_str(), false, useCompression);

    } else if (rxPayload.size == 2 and rxPayload.bytes[0] == 'E') {
        //emergency block, E stands for emergency

#ifdef SOS_BUZZ_PIN

        if (rxPayload.bytes[1] == '!') {
            LOG_INFO("SOS Buzz requested now!");

            auto msg = "Starting SOS Buzzing!";
            sendText(mp.from, 0, msg, false);
            //start emitting sos buzz
            sosBuzzModule->start();

        } else if (rxPayload.bytes[1] == '?') {

            sosBuzzModule->stop();
            auto msg = "Stopped SOS buzzing!";
            sendText(mp.from, 0, msg, false);

        } else {
            LOG_INFO("Unrecognized Emergency command");
        }
#else
        sendText(mp.from, 0, "Current board does not support SOS Buzzing", false, useCompression);
#endif
    }

    return ProcessMessage::CONTINUE; // Let others look at this message also if they want
}

bool ConsoleModule::wantPacket(const meshtastic_MeshPacket *p) {
    return MeshService::isTextPayload(p);
}