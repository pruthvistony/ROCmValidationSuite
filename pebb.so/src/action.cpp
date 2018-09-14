/********************************************************************************
 * 
 * Copyright (c) 2018 ROCm Developer Tools
 *
 * MIT LICENSE:
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is furnished to do
 * so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 *******************************************************************************/
#include "action.h"

extern "C" {
  #include <pci/pci.h>
  #include <linux/pci.h>
}
#include <stdio.h>
#include <stdlib.h>

#include <iostream>
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "pci_caps.h"
#include "gpu_util.h"
#include "rvs_util.h"
#include "rvsloglp.h"
#include "rvshsa.h"
#include "rvstimer.h"
#include "hsa/hsa.h"


#include "rvs_module.h"
#include "worker.h"

#define RVS_CONF_LOG_INTERVAL_KEY "log_interval"
#define DEFAULT_LOG_INTERVAL 1000
#define DEFAULT_DURATION 10000

#define MODULE_NAME "pebb"
#define JSON_CREATE_NODE_ERROR "JSON cannot create node"
#define RVS_CONF_BLOCK_SIZE_KEY "block_size"

using std::cout;
using std::endl;
using std::cerr;
using std::string;
using std::vector;

//! Default constructor
pebbaction::pebbaction() {
  prop_deviceid = -1;
  prop_device_id_filtering = false;
}

//! Default destructor
pebbaction::~pebbaction() {
  property.clear();
}

/**
 *  * @brief reads the module's properties collection to see if
 * device to host transfers will be considered
 */
void pebbaction::property_get_h2d() {
  prop_h2d = true;
  auto it = property.find("host_to_device");
  if (it != property.end()) {
    if (it->second == "false")
      prop_h2d = false;
  }
}

/**
 *  * @brief reads the module's properties collection to see if
 * device to host transfers will be considered
 */
void pebbaction::property_get_d2h() {
  prop_d2h = true;
  auto it = property.find("device_to_host");
  if (it != property.end()) {
    if (it->second == "false")
      prop_d2h = false;
  }
}

/**
 * @brief reads all PQT related configuration keys from
 * the module's properties collection
 * @return true if no fatal error occured, false otherwise
 */
bool pebbaction::get_all_pebb_config_keys(void) {;
  string msg;
  int error;


  RVSTRACE_
  prop_log_interval = property_get_log_interval(&error);
  if (error == 1) {
    cerr << "RVS-PEBB: action: " << action_name <<
    "  invalid '" << RVS_CONF_LOG_INTERVAL_KEY << "' key" << std::endl;
    return false;
  } else if (error == 2) {
    prop_log_interval = DEFAULT_LOG_INTERVAL;
  }

  property_get_h2d();
  property_get_d2h();

  property_get_uint_list(RVS_CONF_BLOCK_SIZE_KEY, YAML_DEVICE_PROP_DELIMITER,
                         &block_size, &b_block_size_all, &error);
  if (error == 1) {
      cerr << "RVS-PEBB: action: " << action_name << "  invalid '"
           << RVS_CONF_BLOCK_SIZE_KEY << "' key" << std::endl;
      return false;
  } else if (error == 2) {
    b_block_size_all = true;
    block_size.clear();
  }

  return true;
}

/**
 * @brief reads all common configuration keys from
 * the module's properties collection
 * @return true if no fatal error occured, false otherwise
 */
bool pebbaction::get_all_common_config_keys(void) {
  string msg, sdevid, sdev;
  int error;

  RVSTRACE_
  // get the action name
  property_get_action_name(&error);
  if (error) {
    msg = "pqt [] action field is missing";
    log(msg.c_str(), rvs::logerror);
    return false;
  }

  // get <device> property value (a list of gpu id)
  if (has_property("device", &sdev)) {
    prop_device_all_selected = property_get_device(&error);
    if (error) {  // log the error & abort GST
      cerr << "RVS-PQT: action: " << action_name <<
      "  invalid 'device' key value " << sdev << std::endl;
      return false;
    }
  } else {
    cerr << "RVS-PQT: action: " << action_name <<
    "  key 'device' was not found" << std::endl;
    return false;
  }

  // get the <deviceid> property value
  if (has_property("deviceid", &sdevid)) {
    int devid = property_get_deviceid(&error);
    if (!error) {
      if (devid != -1) {
        prop_deviceid = static_cast<uint16_t>(devid);
        prop_device_id_filtering = true;
      }
    } else {
      cerr << "RVS-PQT: action: " << action_name <<
      "  invalid 'deviceid' key value " << sdevid << std::endl;
      return false;
    }
  } else {
    prop_device_id_filtering = false;
  }

  // get the other action/GST related properties
  rvs::actionbase::property_get_run_parallel(&error);
  if (error == 1) {
    cerr << "RVS-PQT: action: " << action_name <<
    "  invalid '" << RVS_CONF_PARALLEL_KEY <<
    "' key value" << std::endl;
    return false;
  }

  rvs::actionbase::property_get_run_count(&error);
  if (error == 1) {
      cerr << "RVS-PQT: action: " << action_name <<
          "  invalid '" << RVS_CONF_COUNT_KEY << "' key value" << std::endl;
      return false;
  }

  rvs::actionbase::property_get_run_wait(&error);
  if (error == 1) {
      cerr << "RVS-PQT: action: " << action_name <<
          "  invalid '" << RVS_CONF_WAIT_KEY << "' key value" << std::endl;
      return false;
  }

  rvs::actionbase::property_get_run_duration(&error);
  if (error == 1) {
    cerr << "RVS-PQT: action: " << action_name <<
    "  invalid '" << RVS_CONF_DURATION_KEY <<
    "' key value" << std::endl;
    return false;
  }

  return true;
}

/**
 * @brief Create thread objects based on action description in configuation
 * file.
 *
 * Threads are created but are not started. Execution, one by one of parallel,
 * depends on "parallel" key in configuration file. Pointers to created objects
 * are stored in "test_array" member
 *
 * @return 0 - if successfull, non-zero otherwise
 *
 * */
int pebbaction::create_threads() {
  std::string msg;
  std::vector<uint16_t> gpu_id;
  std::vector<uint16_t> gpu_device_id;
  uint16_t transfer_ix = 0;

  RVSTRACE_
  gpu_get_all_gpu_id(&gpu_id);
  gpu_get_all_device_id(&gpu_device_id);

  for (size_t i = 0; i < gpu_id.size(); i++) {
    if (prop_device_id_filtering) {
      if (prop_deviceid != gpu_device_id[i]) {
        continue;
      }
    }
    // filter out by listed sources
    if (!prop_device_all_selected) {
      const auto it = std::find(device_prop_gpu_id_list.cbegin(),
                                device_prop_gpu_id_list.cend(),
                                std::to_string(gpu_id[i]));
      if (it == device_prop_gpu_id_list.cend()) {
        continue;
      }
    }


    int dstnode;
    int srcnode;

    for (uint cpu_index = 0;
         cpu_index < rvs::hsa::Get()->cpu_list.size();
         cpu_index++) {
      // GPUs are peers, create transaction for them
      dstnode = rvs::gpulist::GetNodeIdFromGpuId(gpu_id[i]);
      if (dstnode < 0) {
        std::cerr << "RVS-PEBB: no node found for destination GPU ID "
          << std::to_string(gpu_id[i]);
        return -1;
      }
      transfer_ix += 1;
      srcnode = rvs::hsa::Get()->cpu_list[cpu_index].node;
      pebbworker* p = new pebbworker;
      p->initialize(srcnode, dstnode, prop_h2d, prop_d2h);
      p->set_name(action_name);
      p->set_stop_name(action_name);
      p->set_transfer_ix(transfer_ix);
      p->set_block_sizes(block_size);
      test_array.push_back(p);
    }
  }

  for (auto it = test_array.begin(); it != test_array.end(); ++it) {
    (*it)->set_transfer_num(test_array.size());
  }

  return 0;
}

/**
 * @brief Delete test thread objects at the end of action execution
 *
 * @return 0 - if successfull, non-zero otherwise
 *
 * */
int pebbaction::destroy_threads() {
  RVSTRACE_
  for (auto it = test_array.begin(); it != test_array.end(); ++it) {
    (*it)->set_stop_name(action_name);
    (*it)->stop();
    delete *it;
  }
  return 0;
}

/**
 * @brief Main action execution entry point. Implements test logic.
 *
 * @return 0 - if successfull, non-zero otherwise
 *
 * */
int pebbaction::run() {
  int sts;
  string msg;

  RVSTRACE_
  if (!get_all_common_config_keys())
    return -1;
  if (!get_all_pebb_config_keys())
    return -1;

  // log_interval must be less than duration
  if (prop_log_interval > 0 && gst_run_duration_ms > 0) {
    if (static_cast<uint64_t>(prop_log_interval) > gst_run_duration_ms) {
      cerr << "RVS-PEBB: action: " << action_name <<
          "  log_interval must be less than duration" << std::endl;
      return -1;
    }
  }

  sts = create_threads();

  if (sts != 0) {
    return sts;
  }

  if (gst_runs_parallel) {
    sts = run_parallel();
  } else {
    sts = run_single();
  }

  destroy_threads();

  return sts;
}

/**
 * @brief Execute test transfers one by one, in round robin fashion, for the
 * duration of the action.
 *
 * @return 0 - if successfull, non-zero otherwise
 *
 * */
int pebbaction::run_single() {
  RVSTRACE_
  // define timers
  rvs::timer<pebbaction> timer_running(&pebbaction::do_running_average, this);
  rvs::timer<pebbaction> timer_final(&pebbaction::do_final_average, this);

  // let the test run
  brun = true;

  unsigned int iter = gst_run_count > 0 ? gst_run_count : 1;
  unsigned int step = gst_run_count == 0 ? 0 : 1;

  // start timers
  if (gst_run_duration_ms) {
    timer_final.start(gst_run_duration_ms, true);  // ticks only once
  }

  if (prop_log_interval) {
    timer_running.start(prop_log_interval);        // ticks continuously
  }

  // iterate through test array and invoke tests one by one
  do {
    for (auto it = test_array.begin(); brun && it != test_array.end(); ++it) {
      (*it)->do_transfer();

      // if log interval is zero, print current results immediately
      if (prop_log_interval == 0) {
        print_running_average(*it);
      }
      sleep(1);

      if (rvs::lp::Stopping()) {
        brun = false;
        break;
      }
    }

    iter -= step;

    // insert wait between runs if needed
    if (iter > 0 && gst_run_wait_ms > 0) {
      sleep(gst_run_wait_ms);
    }
  } while (brun && iter);

  timer_running.stop();
  timer_final.stop();

  print_final_average();

  return rvs::lp::Stopping() ? -1 : 0;
}

/**
 * @brief Execute test transfers all at once, for the
 * duration of the action.
 *
 * @return 0 - if successfull, non-zero otherwise
 *
 * */
int pebbaction::run_parallel() {
  RVSTRACE_
  // define timers
  rvs::timer<pebbaction> timer_running(&pebbaction::do_running_average, this);
  rvs::timer<pebbaction> timer_final(&pebbaction::do_final_average, this);

  // let the test run
  brun = true;

  // start all worker threads
  for (auto it = test_array.begin(); it != test_array.end(); ++it) {
    (*it)->start();
  }

  // start timers
  if (gst_run_duration_ms) {
    timer_final.start(gst_run_duration_ms, true);  // ticks only once
  }

  if (prop_log_interval) {
    timer_running.start(prop_log_interval);        // ticks continuously
  }

  // wait for test to complete
  while (brun) {
    if (rvs::lp::Stopping()) {
      RVSTRACE_
      brun = false;
    }
    sleep(1);
  }

  timer_running.stop();
  timer_final.stop();

  // signal all worker threads to stop
  for (auto it = test_array.begin(); it != test_array.end(); ++it) {
    (*it)->stop();
  }
  sleep(10);

  // join all worker threads
  for (auto it = test_array.begin(); it != test_array.end(); ++it) {
    (*it)->join();
  }

  print_final_average();

  return rvs::lp::Stopping() ? -1 : 0;
}

/**
 * @brief Collect running average bandwidth data for all the tests and prints
 * them out.
 *
 * @return 0 - if successfull, non-zero otherwise
 *
 * */
int pebbaction::print_running_average() {
  for (auto it = test_array.begin(); brun && it != test_array.end(); ++it) {
    print_running_average(*it);
  }

  return 0;
}

/**
 * @brief Collect running average for this particular transfer.
 *
 * @param pWorker ptr to a pebbworker class
 *
 * @return 0 - if successfull, non-zero otherwise
 *
 * */
int pebbaction::print_running_average(pebbworker* pWorker) {
  int         src_node, dst_node;
  int         dst_id;
  bool        bidir;
  size_t      current_size;
  double      duration;
  std::string msg;
  char        buff[64];
  double      bandwidth;
  uint16_t    transfer_ix;
  uint16_t    transfer_num;

  // get running average
  pWorker->get_running_data(&src_node, &dst_node, &bidir,
                            &current_size, &duration);

  if (duration > 0) {
    bandwidth = current_size/duration/(1024*1024*1024);
    if (bidir) {
      bandwidth *=2;
    }
    snprintf( buff, sizeof(buff), "%.3f GBps", bandwidth);
  } else {
    // no running average in this iteration, try getting total so far
    // (do not reset final totals as this is just intermediate query)
    pWorker->get_final_data(&src_node, &dst_node, &bidir,
                            &current_size, &duration, false);
    if (duration > 0) {
      bandwidth = current_size/duration/(1024*1024*1024);
      if (bidir) {
        bandwidth *=2;
      }
      snprintf( buff, sizeof(buff), "%.3f GBps (*)", bandwidth);
    } else {
      // not transfers at all - print "pending"
      snprintf( buff, sizeof(buff), "(pending)");
    }
  }

  dst_id = rvs::gpulist::GetGpuIdFromNodeId(dst_node);
  transfer_ix = pWorker->get_transfer_ix();
  transfer_num = pWorker->get_transfer_num();

  msg = "[" + action_name + "] pcie-bandwidth  ["
      + std::to_string(transfer_ix) + "/" + std::to_string(transfer_num)
      + "] "
      + std::to_string(src_node) + " " + std::to_string(dst_id)
      + "  h2d: " + (prop_h2d ? "true" : "false")
      + "  d2h: " + (prop_d2h ? "true" : "false") + "  "
      + buff;

  rvs::lp::Log(msg, rvs::loginfo);

  if (bjson) {
    unsigned int sec;
    unsigned int usec;
    rvs::lp::get_ticks(&sec, &usec);
    json_rcqt_node = rvs::lp::LogRecordCreate(MODULE_NAME,
                        action_name.c_str(), rvs::loginfo, sec, usec);
    if (json_rcqt_node != NULL) {
      rvs::lp::AddString(json_rcqt_node,
                          "transfer_ix", std::to_string(transfer_ix));
      rvs::lp::AddString(json_rcqt_node,
                          "transfer_num", std::to_string(transfer_num));
      rvs::lp::AddString(json_rcqt_node, "src", std::to_string(src_node));
      rvs::lp::AddString(json_rcqt_node, "dst", std::to_string(dst_id));
      rvs::lp::AddString(json_rcqt_node, "pcie-bandwidth (GBps)", buff);
      rvs::lp::LogRecordFlush(json_rcqt_node);
    }
  }

  return 0;
}

/**
 * @brief Collect bandwidth totals for all the tests and prints
 * them on cout at the end of action execution
 *
 * @return 0 - if successfull, non-zero otherwise
 *
 * */
int pebbaction::print_final_average() {
  int         src_node, dst_node;
  int         dst_id;
  bool        bidir;
  size_t      current_size;
  double      duration;
  std::string msg;
  double      bandwidth;
  char        buff[128];
  uint16_t    transfer_ix;
  uint16_t    transfer_num;

  for (auto it = test_array.begin(); it != test_array.end(); ++it) {
    (*it)->get_final_data(&src_node, &dst_node, &bidir,
                          &current_size, &duration);

    if (duration) {
      bandwidth = current_size/duration/(1024*1024*1024);
      if (bidir) {
        bandwidth *=2;
      }
      snprintf( buff, sizeof(buff), "%.3f GBps", bandwidth);
    } else {
      snprintf( buff, sizeof(buff), "(not measured)");
    }

    dst_id = rvs::gpulist::GetGpuIdFromNodeId(dst_node);
    transfer_ix = (*it)->get_transfer_ix();
    transfer_num = (*it)->get_transfer_num();

    msg = "[" + action_name + "] pcie-bandwidth  ["
        + std::to_string(transfer_ix) + "/" + std::to_string(transfer_num)
        + "] "
        + std::to_string(src_node) + " " + std::to_string(dst_id)
        + "  h2d: " + (prop_h2d ? "true" : "false")
        + "  d2h: " + (prop_d2h ? "true" : "false")
        + "  " + buff
        + "  duration: " + std::to_string(duration) + " sec";

    rvs::lp::Log(msg, rvs::logresults);
    if (bjson) {
      unsigned int sec;
      unsigned int usec;
      rvs::lp::get_ticks(&sec, &usec);
      json_rcqt_node = rvs::lp::LogRecordCreate(MODULE_NAME,
                          action_name.c_str(), rvs::logresults, sec, usec);
      if (json_rcqt_node != NULL) {
        rvs::lp::AddString(json_rcqt_node,
                            "transfer_ix", std::to_string(transfer_ix));
        rvs::lp::AddString(json_rcqt_node,
                            "transfer_num", std::to_string(transfer_num));
        rvs::lp::AddString(json_rcqt_node, "src", std::to_string(src_node));
        rvs::lp::AddString(json_rcqt_node, "dst", std::to_string(dst_id));
        rvs::lp::AddString(json_rcqt_node, "bandwidth (GBps)", buff);
        rvs::lp::AddString(json_rcqt_node, "duration (sec)",
                           std::to_string(duration));
        rvs::lp::LogRecordFlush(json_rcqt_node);
      }
    }
  }
  return 0;
}

/**
 * @brief timer callback used to signal end of test
 *
 * timer callback used to signal end of test and to initiate
 * calculation of final average
 *
 * */
void pebbaction::do_final_average() {
  std::string msg;
  unsigned int sec;
  unsigned int usec;
  rvs::lp::get_ticks(&sec, &usec);

  msg = "[" + action_name + "] pebb in do_final_average";
  rvs::lp::Log(msg, rvs::logtrace, sec, usec);

  if (bjson) {
    json_rcqt_node = rvs::lp::LogRecordCreate(MODULE_NAME,
                            action_name.c_str(), rvs::logtrace, sec, usec);
    if (json_rcqt_node != NULL) {
      rvs::lp::AddString(json_rcqt_node, "message", "pebb in do_final_average");
      rvs::lp::LogRecordFlush(json_rcqt_node);
    }
  }

  brun = false;
}

/**
 * @brief timer callback used to signal end of log interval
 *
 * timer callback used to signal end of log interval and to initiate
 * calculation of moving average
 *
 * */
void pebbaction::do_running_average() {
  unsigned int sec;
  unsigned int usec;
  std::string msg;

  rvs::lp::get_ticks(&sec, &usec);
  msg = "[" + action_name + "] pebb in do_running_average";
  rvs::lp::Log(msg, rvs::logtrace, sec, usec);
  if (bjson) {
    json_rcqt_node = rvs::lp::LogRecordCreate(MODULE_NAME,
                            action_name.c_str(), rvs::logtrace, sec, usec);
    if (json_rcqt_node != NULL) {
      rvs::lp::AddString(json_rcqt_node,
                         "message",
                         "in do_running_average");
      rvs::lp::LogRecordFlush(json_rcqt_node);
    }
  }
  print_running_average();
}
