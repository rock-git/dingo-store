// Copyright (c) 2023 dingodb.com, Inc. All Rights Reserved
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

#ifndef DINGODB_SDK_TEST_MOCK_RPC_INTERACTION_H_
#define DINGODB_SDK_TEST_MOCK_RPC_INTERACTION_H_

#include <memory>

#include "brpc/channel.h"
#include "gmock/gmock.h"
#include "rpc/rpc_interaction.h"

namespace dingodb {
namespace sdk {
class MockRpcInteraction final : public RpcInteraction {
 public:
  MockRpcInteraction(brpc::ChannelOptions options) : RpcInteraction(std::move(options)) {}

  ~MockRpcInteraction() override = default;

  MOCK_METHOD(Status, SendRpc, (Rpc& rpc, google::protobuf::Closure* done), (override));

  MOCK_METHOD(Status, InitChannel, (const butil::EndPoint& server_addr_and_port, std::shared_ptr<brpc::Channel>& channel),
              (override));

  Status SendRpcSync(Rpc& rpc, google::protobuf::Closure* done = nullptr) { return SendRpc(rpc, done); }

  Status RealSendRpc(Rpc& rpc, google::protobuf::Closure* done = nullptr) { return RpcInteraction::SendRpc(rpc, done); }

  Status RealInitChannel(const butil::EndPoint& server_addr_and_port, std::shared_ptr<brpc::Channel>& channel) {
    return RpcInteraction::InitChannel(server_addr_and_port, channel);
  }
};
}  // namespace sdk

}  // namespace dingodb

#endif  // DINGODB_SDK_TEST_MOCK_RPC_INTERACTION_H_