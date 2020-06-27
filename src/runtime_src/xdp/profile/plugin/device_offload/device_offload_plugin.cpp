/**
 * Copyright (C) 2020 Xilinx, Inc
 *
 * Licensed under the Apache License, Version 2.0 (the "License"). You may
 * not use this file except in compliance with the License. A copy of the
 * License is located at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 * License for the specific language governing permissions and limitations
 * under the License.
 */

#define XDP_SOURCE

#include <string>
#include <sstream>

#include "xdp/profile/database/database.h"
#include "xdp/profile/plugin/device_offload/device_offload_plugin.h"
#include "xdp/profile/plugin/vp_base/utility.h"
#include "xdp/profile/writer/device_trace/device_trace_writer.h"
#include "xdp/profile/database/events/creator/device_event_trace_logger.h"

#include "core/common/config_reader.h"

namespace xdp {

  DeviceOffloadPlugin::DeviceOffloadPlugin() : XDPPlugin()
  {
    active = db->claimDeviceOffloadOwnership() ;
    if (!active) return ; 

    db->registerPlugin(this) ;

    // Get the profiling continuous offload options from xrt.ini
    std::string tb_size = xrt_core::config::get_trace_buffer_size() ;
    std::stringstream convert ;
    convert << tb_size ;
    convert >> trace_buffer_size ;
    
    continuous_trace = xrt_core::config::get_continuous_trace ;
    continuous_trace_interval_ms = 
      xrt_core::config::get_continuous_trace_interval_ms() ;
  }

  DeviceOffloadPlugin::~DeviceOffloadPlugin()
  {
  }

  void DeviceOffloadPlugin::addDevice(const std::string& sysfsPath)
  {
    if (!active) return ;

    uint64_t deviceId = db->addDevice(sysfsPath) ;

    // When adding a device, also add a writer to dump the information
    std::string version = "1.0" ;
    std::string creationTime = xdp::getCurrentDateTime() ;
    std::string xrtVersion   = xdp::getXRTVersion() ;
    std::string toolVersion  = xdp::getToolVersion() ;

    std::string filename = 
      "device_trace_" + std::to_string(deviceId) + ".csv" ;
      
    writers.push_back(new DeviceTraceWriter(filename.c_str(),
					    deviceId,
					    version,
					    creationTime,
					    xrtVersion,
					    toolVersion)) ;

    (db->getStaticInfo()).addOpenedFile(filename.c_str(), "VP_TRACE") ;
  }

  // It is the responsibility of the child class to instantiate the appropriate
  //  device interface based on the level (OpenCL or HAL)
  void DeviceOffloadPlugin::addOffloader(uint64_t deviceId,
					 DeviceIntf* devInterface)
  {
    if (!active) return ;

    TraceLoggerCreatingDeviceEvents* logger = 
      new TraceLoggerCreatingDeviceEvents(deviceId) ;

     DeviceTraceOffload* offloader = 
      new DeviceTraceOffload(devInterface,
			     logger,
			     continuous_trace_interval_ms, // offload_sleep_ms,
			     trace_buffer_size,            // trbuf_size,
			     continuous_trace) ;           // start_thread

     offloader->read_trace_init() ;

     offloaders[deviceId] = offloader ;
  }
  
  void DeviceOffloadPlugin::configureTraceIP(DeviceIntf* devInterface)
  {
    // Collect all the profiling options from xrt.ini
    std::string data_transfer_trace = 
      xrt_core::config::get_data_transfer_trace() ;
    std::string stall_trace = xrt_core::config::get_stall_trace() ;

    // Set up the hardware trace option
    uint32_t traceOption = 0 ;
    
    // Bit 1: 1 = Coarse mode, 0 = Fine mode 
    if (data_transfer_trace == "coarse") traceOption |= 0x1 ;
    
    // Bit 2: 1 = Device trace enabled, 0 = Device trace disabled
    if (data_transfer_trace != "off")    traceOption |= 0x2 ;
    
    // Bit 3: 1 = Pipe stalls enabled, 0 = Pipe stalls disabled
    if (stall_trace == "pipe" || stall_trace == "all") traceOption |= 0x4 ;
    
    // Bit 4: 1 = Dataflow stalls enabled, 0 = Dataflow stalls disabled
    if (stall_trace == "dataflow" || stall_trace == "all") traceOption |= 0x8;
    
    // Bit 5: 1 = Memory stalls enabled, 0 = Memory stalls disabled
    if (stall_trace == "memory" || stall_trace == "all") traceOption |= 0x10 ;

    devInterface->startTrace(traceOption) ;
  }

  void DeviceOffloadPlugin::writeAll(bool openNewFiles)
  {
    if (!active) return ;

    // This function gets called if the database is destroyed before
    //  the plugin object.  At this time, the information in the database
    //  still exists and is viable, so we should flush our devices
    //  and write our writers.
    for (auto o : offloaders)
    {
      (o.second)->read_trace() ;
    }    

    for (auto w : writers)
    {
      w->write(openNewFiles) ;
    }
  }
  
} // end namespace xdp