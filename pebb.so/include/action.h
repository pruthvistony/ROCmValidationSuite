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
#ifndef PEBB_SO_INCLUDE_ACTION_H_
#define PEBB_SO_INCLUDE_ACTION_H_

#include <unistd.h>
#include <stdlib.h>
#include <assert.h>

#include <algorithm>
#include <cctype>
#include <sstream>
#include <limits>
#include <string>
#include <vector>

#include "rvsactionbase.h"
#include "worker.h"
#include "hsa/hsa.h"
#include "hsa/hsa_ext_amd.h"


/**
 * @class pebbaction
 * @ingroup PEBB
 *
 * @brief PEBB action implementation class
 *
 * Derives from rvs::actionbase and implements actual action functionality
 * in its run() method.
 *
 */
class pebbaction : public rvs::actionbase {
 public:
  pebbaction();
  virtual ~pebbaction();

  virtual int run(void);

 protected:
  bool get_all_pebb_config_keys(void);
  bool get_all_common_config_keys(void);
  //! 'true' if "all" is found under "device" key for this action
  bool      prop_device_all_selected;
  //! 'true' if "all" is found under "peer" key for this action
  bool      prop_peer_device_all_selected;
  //! deviceid key from config file
  uint16_t  prop_deviceid;
  //! 'true' if prop_device_id is valid number
  bool      prop_device_id_filtering;

  // PQT specific config keys
  void property_get_h2d();
  void property_get_d2h();

  //! array of peer GPU IDs to be used in data trasfers
  std::vector<std::string> prop_peers;
  //! deviceid of peer GPUs
  int  prop_peer_deviceid;
  //! 'true' if bandwidth test is to be executed for verified peers
  bool prop_test_bandwidth;
  //! log interval for running totals (in msec)
  int  prop_log_interval;
  //! 'true' if bidirectional data transfer is required
  bool prop_bidirectional;

  //! 'true' if host to device transfer is required
  bool prop_h2d;
  //! 'true' if device to host transfer is required
  bool prop_d2h;

 protected:
  int create_threads();
  int destroy_threads();

  int run_single();
  int run_parallel();

  int print_running_average();
  int print_running_average(pebbworker* pWorker);
  int print_final_average();

  //! 'true' for the duration of test
  bool brun;
  //! bjson field indicates if the json flag is set
  bool bjson;
  //! json_rcqt_node is json node shared through submodules
  void *json_rcqt_node;

 private:
  void do_running_average(void);
  void do_final_average(void);

  std::vector<pebbworker*> test_array;
};

#endif  // PEBB_SO_INCLUDE_ACTION_H_
