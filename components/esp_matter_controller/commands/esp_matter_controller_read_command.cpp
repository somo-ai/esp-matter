// Copyright 2022 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <controller/CommissioneeDeviceProxy.h>
#include <esp_log.h>
#include <esp_matter_client.h>
#include <esp_matter_controller_client.h>
#include <esp_matter_controller_read_command.h>

#include <app/server/Server.h>

#include <commands/clusters/DataModelLogger.h>

using namespace chip::app::Clusters;
using namespace esp_matter::client;
using chip::DeviceProxy;
using chip::app::InteractionModelEngine;
using chip::app::ReadClient;
using chip::app::ReadPrepareParams;

static const char *TAG = "read_command";

namespace esp_matter {
namespace controller {

void read_command::on_device_connected_fcn(void *context, ExchangeManager &exchangeMgr,
                                           const SessionHandle &sessionHandle)
{
    read_command *cmd = (read_command *)context;
    chip::OperationalDeviceProxy device_proxy(&exchangeMgr, sessionHandle);
    esp_err_t err = interaction::read::send_request(&device_proxy, cmd->m_attr_paths.Get(),
                                                    cmd->m_attr_paths.AllocatedSize(), cmd->m_event_paths.Get(),
                                                    cmd->m_event_paths.AllocatedSize(), cmd->m_buffered_read_cb);
    if (err != ESP_OK) {
        chip::Platform::Delete(cmd);
    }
    return;
}

void read_command::on_device_connection_failure_fcn(void *context, const ScopedNodeId &peerId, CHIP_ERROR error)
{
    read_command *cmd = (read_command *)context;
    ESP_LOGE(TAG, "Read: CASE session failed: node=0x%" PRIX64 " err=%s",
             peerId.GetNodeId(), chip::ErrorStr(error));
    chip::Platform::Delete(cmd);
}

esp_err_t read_command::send_command()
{
#ifdef CONFIG_ESP_MATTER_ENABLE_MATTER_SERVER
    chip::Server &server = chip::Server::GetInstance();
    server.GetCASESessionManager()->FindOrEstablishSession(ScopedNodeId(m_node_id, get_fabric_index()),
                                                           &on_device_connected_cb, &on_device_connection_failure_cb);
    return ESP_OK;
#else
    auto &controller_instance = esp_matter::controller::matter_controller_client::get_instance();
#ifdef CONFIG_ESP_MATTER_COMMISSIONER_ENABLE
    if (CHIP_NO_ERROR ==
        controller_instance.get_commissioner()->GetConnectedDevice(m_node_id, &on_device_connected_cb,
                                                                   &on_device_connection_failure_cb)) {
        return ESP_OK;
    }
#else
    if (CHIP_NO_ERROR ==
        controller_instance.get_controller()->GetConnectedDevice(m_node_id, &on_device_connected_cb,
                                                                 &on_device_connection_failure_cb)) {
        return ESP_OK;
    }
#endif // CONFIG_ESP_MATTER_COMMISSIONER_ENABLE
#endif // CONFIG_ESP_MATTER_ENABLE_MATTER_SERVER

    chip::Platform::Delete(this);
    return ESP_FAIL;
}

// ReadClient Callback Interface
void read_command::OnAttributeData(const chip::app::ConcreteDataAttributePath &path, chip::TLV::TLVReader *data,
                                   const chip::app::StatusIB &status)
{
    CHIP_ERROR error = status.ToChipError();
    if (CHIP_NO_ERROR != error) {
        ESP_LOGE(TAG, "Response Failure: %s", chip::ErrorStr(error));
        return;
    }

    if (data == nullptr) {
        ESP_LOGE(TAG, "Response Failure: No Data");
        return;
    }

    // Log the raw attribute value at WARN level so it's visible even with
    // CONFIG_CHIP_LOG_DEFAULT_LEVEL_ERROR (DataModelLogger uses ChipLogProgress
    // which is suppressed).
    {
        // Cluster name lookup for common Matter clusters
        auto cluster_name = [](uint32_t id) -> const char * {
            switch (id) {
            case 0x001D: return "Descriptor";
            case 0x001E: return "Binding";
            case 0x001F: return "AccessControl";
            case 0x0028: return "BasicInformation";
            case 0x002A: return "OtaSoftwareUpdate";
            case 0x002B: return "LocalizationConfig";
            case 0x002F: return "PowerSource";
            case 0x0030: return "GeneralCommissioning";
            case 0x0031: return "NetworkCommissioning";
            case 0x0033: return "GeneralDiagnostics";
            case 0x0034: return "SoftwareDiagnostics";
            case 0x0035: return "ThreadDiagnostics";
            case 0x0037: return "EthernetDiagnostics";
            case 0x003C: return "AdminCommissioning";
            case 0x003E: return "OperationalCredentials";
            case 0x003F: return "GroupKeyManagement";
            case 0x0040: return "FixedLabel";
            case 0x0041: return "UserLabel";
            case 0x0045: return "BooleanState";
            case 0x0046: return "ICDManagement";
            case 0x0003: return "Identify";
            case 0x0004: return "Groups";
            case 0x0005: return "Scenes";
            case 0x0006: return "OnOff";
            case 0x0008: return "LevelControl";
            case 0x0101: return "DoorLock";
            case 0x0102: return "WindowCovering";
            case 0x0200: return "PumpConfigControl";
            case 0x0201: return "Thermostat";
            case 0x0204: return "ThermostatUI";
            case 0x0300: return "ColorControl";
            case 0x0400: return "IlluminanceMeasurement";
            case 0x0402: return "TemperatureMeasurement";
            case 0x0403: return "PressureMeasurement";
            case 0x0405: return "RelativeHumidity";
            case 0x0406: return "OccupancySensing";
            default:     return nullptr;
            }
        };

        // Attribute name lookup for frequently used attributes
        auto attr_name = [](uint32_t cluster, uint32_t attr) -> const char * {
            switch (cluster) {
            case 0x001D: // Descriptor
                switch (attr) {
                case 0x0000: return "DeviceTypeList";
                case 0x0001: return "ServerList";
                case 0x0002: return "ClientList";
                case 0x0003: return "PartsList";
                default: return nullptr;
                }
            case 0x0006: // OnOff
                switch (attr) {
                case 0x0000: return "OnOff";
                case 0x4000: return "GlobalSceneControl";
                case 0x4001: return "OnTime";
                case 0x4002: return "OffWaitTime";
                case 0x4003: return "StartUpOnOff";
                default: return nullptr;
                }
            case 0x0008: // LevelControl
                switch (attr) {
                case 0x0000: return "CurrentLevel";
                case 0x0001: return "RemainingTime";
                case 0x0002: return "MinLevel";
                case 0x0003: return "MaxLevel";
                case 0x0011: return "OnLevel";
                case 0x4000: return "StartUpCurrentLevel";
                default: return nullptr;
                }
            case 0x0028: // BasicInformation
                switch (attr) {
                case 0x0000: return "DataModelRevision";
                case 0x0001: return "VendorName";
                case 0x0002: return "VendorID";
                case 0x0003: return "ProductName";
                case 0x0004: return "ProductID";
                case 0x0005: return "NodeLabel";
                case 0x0006: return "Location";
                case 0x0007: return "HardwareVersion";
                case 0x0008: return "HardwareVersionString";
                case 0x0009: return "SoftwareVersion";
                case 0x000A: return "SoftwareVersionString";
                case 0x000F: return "SerialNumber";
                case 0x0012: return "UniqueID";
                default: return nullptr;
                }
            case 0x002F: // PowerSource
                switch (attr) {
                case 0x0000: return "Status";
                case 0x000B: return "BatVoltage";
                case 0x000C: return "BatPercentRemaining";
                case 0x000E: return "BatChargeLevel";
                default: return nullptr;
                }
            case 0x0045: // BooleanState
                switch (attr) {
                case 0x0000: return "StateValue";
                default: return nullptr;
                }
            case 0x0300: // ColorControl
                switch (attr) {
                case 0x0000: return "CurrentHue";
                case 0x0001: return "CurrentSaturation";
                case 0x0007: return "ColorTemperatureMireds";
                case 0x0008: return "ColorMode";
                case 0x400B: return "ColorTempPhysicalMin";
                case 0x400C: return "ColorTempPhysicalMax";
                default: return nullptr;
                }
            case 0x0402: // TemperatureMeasurement
                switch (attr) {
                case 0x0000: return "MeasuredValue";
                case 0x0001: return "MinMeasuredValue";
                case 0x0002: return "MaxMeasuredValue";
                default: return nullptr;
                }
            default: return nullptr;
            }
        };

        const char *cn = cluster_name(path.mClusterId);
        const char *an = attr_name(path.mClusterId, path.mAttributeId);
        char label[80];
        snprintf(label, sizeof(label), "ep:%u %s%s0x%04" PRIX32 ".%s%s0x%04" PRIX32,
                 path.mEndpointId,
                 cn ? cn : "", cn ? "/" : "", path.mClusterId,
                 an ? an : "", an ? "/" : "", path.mAttributeId);

        chip::TLV::TLVReader peek;
        peek.Init(*data);
        chip::TLV::TLVType type = peek.GetType();
        switch (type) {
        case chip::TLV::kTLVType_Boolean: {
            bool val;
            if (peek.Get(val) == CHIP_NO_ERROR) {
                ESP_LOGW(TAG, "  %s => %s", label, val ? "TRUE" : "FALSE");
            }
            break;
        }
        case chip::TLV::kTLVType_UnsignedInteger: {
            uint64_t val;
            if (peek.Get(val) == CHIP_NO_ERROR) {
                ESP_LOGW(TAG, "  %s => %" PRIu64 " (0x%" PRIX64 ")", label, val, val);
            }
            break;
        }
        case chip::TLV::kTLVType_SignedInteger: {
            int64_t val;
            if (peek.Get(val) == CHIP_NO_ERROR) {
                ESP_LOGW(TAG, "  %s => %" PRId64, label, val);
            }
            break;
        }
        case chip::TLV::kTLVType_UTF8String: {
            char buf[128];
            uint32_t len = sizeof(buf) - 1;
            if (peek.GetString(buf, len) == CHIP_NO_ERROR) {
                ESP_LOGW(TAG, "  %s => \"%s\"", label, buf);
            }
            break;
        }
        case chip::TLV::kTLVType_Array: {
            chip::TLV::TLVType containerType;
            if (peek.EnterContainer(containerType) == CHIP_NO_ERROR) {
                char list_buf[512];
                int off = 0;
                while (peek.Next() == CHIP_NO_ERROR && off < (int)sizeof(list_buf) - 40) {
                    uint64_t elem;
                    if (peek.Get(elem) == CHIP_NO_ERROR) {
                        // For ServerList/ClientList, resolve cluster names
                        const char *ename = (path.mAttributeId == 0x0001 || path.mAttributeId == 0x0002)
                                            ? cluster_name((uint32_t)elem) : nullptr;
                        int written;
                        if (ename) {
                            written = snprintf(list_buf + off, sizeof(list_buf) - off,
                                               "%s(0x%04" PRIX64 ") ", ename, elem);
                        } else {
                            written = snprintf(list_buf + off, sizeof(list_buf) - off,
                                               "0x%04" PRIX64 " ", elem);
                        }
                        if (written > 0) off += written;
                    }
                }
                list_buf[off] = '\0';
                ESP_LOGW(TAG, "  %s => [%s]", label, list_buf);
                peek.ExitContainer(containerType);
            }
            break;
        }
        default:
            ESP_LOGW(TAG, "  %s => (TLV type %d)", label, static_cast<int>(type));
            break;
        }
    }

    if (attribute_data_cb) {
        chip::TLV::TLVReader data_cpy;
        data_cpy.Init(*data);
        attribute_data_cb(m_node_id, path, &data_cpy);
    }
    error = DataModelLogger::LogAttribute(path, data);
    if (CHIP_NO_ERROR != error) {
        ESP_LOGE(TAG, "Response Failure: Can not decode Data");
    }
}

void read_command::OnEventData(const chip::app::EventHeader &event_header, chip::TLV::TLVReader *data,
                               const chip::app::StatusIB *status)
{
    CHIP_ERROR error = CHIP_NO_ERROR;
    if (status != nullptr) {
        error = status->ToChipError();
        if (CHIP_NO_ERROR != error) {
            ESP_LOGE(TAG, "Response Failure: %s", chip::ErrorStr(error));
            return;
        }
    }

    if (data == nullptr) {
        ESP_LOGE(TAG, "Response Failure: No Data");
        return;
    }
    if (event_data_cb) {
        chip::TLV::TLVReader data_cpy;
        data_cpy.Init(*data);
        event_data_cb(m_node_id, event_header, &data_cpy);
    }
    error = DataModelLogger::LogEvent(event_header, data);
    if (CHIP_NO_ERROR != error) {
        ESP_LOGE(TAG, "Response Failure: Can not decode Data");
    }
}

void read_command::OnError(CHIP_ERROR error)
{
    ESP_LOGE(TAG, "Read Error: %s", chip::ErrorStr(error));
}

void read_command::OnDeallocatePaths(chip::app::ReadPrepareParams &&aReadPrepareParams)
{
    // Intentionally empty because the AttributePathParamsList or EventPathParamsList will be deleted with the
    // read_command.
}

void read_command::OnDone(ReadClient *apReadClient)
{
    ESP_LOGI(TAG, "read done");
    if (read_done_cb) {
        read_done_cb(m_node_id, m_attr_paths, m_event_paths);
    }
    chip::Platform::Delete(this);
}

esp_err_t send_read_attr_command(uint64_t node_id, ScopedMemoryBufferWithSize<uint16_t> &endpoint_ids,
                                 ScopedMemoryBufferWithSize<uint32_t> &cluster_ids,
                                 ScopedMemoryBufferWithSize<uint32_t> &attribute_ids)
{
    if (endpoint_ids.AllocatedSize() != cluster_ids.AllocatedSize() ||
        endpoint_ids.AllocatedSize() != attribute_ids.AllocatedSize()) {
        ESP_LOGE(TAG,
                 "The endpoint_id array length should be the same as the cluster_ids array length"
                 "and the attribute_ids array length");
        return ESP_ERR_INVALID_ARG;
    }
    ScopedMemoryBufferWithSize<AttributePathParams> attr_paths;
    ScopedMemoryBufferWithSize<EventPathParams> event_paths;
    attr_paths.Alloc(endpoint_ids.AllocatedSize());
    if (!attr_paths.Get()) {
        ESP_LOGE(TAG, "Failed to alloc memory for attribute paths");
        return ESP_ERR_NO_MEM;
    }
    for (size_t i = 0; i < attr_paths.AllocatedSize(); ++i) {
        attr_paths[i] = AttributePathParams(endpoint_ids[i], cluster_ids[i], attribute_ids[i]);
    }

    read_command *cmd = chip::Platform::New<read_command>(node_id, std::move(attr_paths), std::move(event_paths),
                                                          nullptr, nullptr, nullptr);
    if (!cmd) {
        ESP_LOGE(TAG, "Failed to alloc memory for read_command");
        return ESP_ERR_NO_MEM;
    }
    return cmd->send_command();
}

esp_err_t send_read_event_command(uint64_t node_id, ScopedMemoryBufferWithSize<uint16_t> &endpoint_ids,
                                  ScopedMemoryBufferWithSize<uint32_t> &cluster_ids,
                                  ScopedMemoryBufferWithSize<uint32_t> &event_ids)
{
    if (endpoint_ids.AllocatedSize() != cluster_ids.AllocatedSize() ||
        endpoint_ids.AllocatedSize() != event_ids.AllocatedSize()) {
        ESP_LOGE(TAG,
                 "The endpoint_id array length should be the same as the cluster_ids array length"
                 "and the attribute_ids array length");
        return ESP_ERR_INVALID_ARG;
    }
    ScopedMemoryBufferWithSize<AttributePathParams> attr_paths;
    ScopedMemoryBufferWithSize<EventPathParams> event_paths;
    event_paths.Alloc(endpoint_ids.AllocatedSize());
    if (!event_paths.Get()) {
        ESP_LOGE(TAG, "Failed to alloc memory for attribute paths");
        return ESP_ERR_NO_MEM;
    }
    for (size_t i = 0; i < event_paths.AllocatedSize(); ++i) {
        event_paths[i] = EventPathParams(endpoint_ids[i], cluster_ids[i], event_ids[i]);
    }

    read_command *cmd = chip::Platform::New<read_command>(node_id, std::move(attr_paths), std::move(event_paths),
                                                          nullptr, nullptr, nullptr);
    if (!cmd) {
        ESP_LOGE(TAG, "Failed to alloc memory for read_command");
        return ESP_ERR_NO_MEM;
    }
    return cmd->send_command();
}

esp_err_t send_read_attr_command(uint64_t node_id, uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id)
{
    ScopedMemoryBufferWithSize<uint16_t> endpoint_ids;
    ScopedMemoryBufferWithSize<uint32_t> cluster_ids;
    ScopedMemoryBufferWithSize<uint32_t> attribute_ids;
    endpoint_ids.Alloc(1);
    cluster_ids.Alloc(1);
    attribute_ids.Alloc(1);
    if (!(endpoint_ids.Get() && cluster_ids.Get() && attribute_ids.Get())) {
        return ESP_ERR_NO_MEM;
    } else {
        *endpoint_ids.Get() = endpoint_id;
        *cluster_ids.Get() = cluster_id;
        *attribute_ids.Get() = attribute_id;
    }
    return send_read_attr_command(node_id, endpoint_ids, cluster_ids, attribute_ids);
}

esp_err_t send_read_event_command(uint64_t node_id, uint16_t endpoint_id, uint32_t cluster_id, uint32_t event_id)
{
    ScopedMemoryBufferWithSize<uint16_t> endpoint_ids;
    ScopedMemoryBufferWithSize<uint32_t> cluster_ids;
    ScopedMemoryBufferWithSize<uint32_t> event_ids;
    endpoint_ids.Alloc(1);
    cluster_ids.Alloc(1);
    event_ids.Alloc(1);
    if (!(endpoint_ids.Get() && cluster_ids.Get() && event_ids.Get())) {
        return ESP_ERR_NO_MEM;
    } else {
        *endpoint_ids.Get() = endpoint_id;
        *cluster_ids.Get() = cluster_id;
        *event_ids.Get() = event_id;
    }
    return send_read_event_command(node_id, endpoint_ids, cluster_ids, event_ids);
}

} // namespace controller
} // namespace esp_matter
