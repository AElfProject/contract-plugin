/*
 *
 * Copyright 2015 gRPC authors. Modified by AElfProject.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifndef GRPC_INTERNAL_COMPILER_CONTRACT_CSHARP_GENERATOR_H
#define GRPC_INTERNAL_COMPILER_CONTRACT_CSHARP_GENERATOR_H

#include "config.h"

#include <google/protobuf/compiler/csharp/csharp_names.h>
#include <google/protobuf/compiler/csharp/csharp_helpers.h>

namespace grpc_contract_csharp_generator {

  const unsigned char GENERATE_CONTRACT = 0x1; // hex for 0000 0001
  const unsigned char GENERATE_TESTER = 0x2; // hex for 0000 0010
  const unsigned char GENERATE_REFERENCE = 0x4; // hex for 0000 0100
  const unsigned char GENERATE_EVENT = 0x8; // hex for 0000 1000
  const unsigned char INTERNAL_ACCESS = 0x80; // hex for 1000 0000
  const unsigned char GENERATE_CONTRACT_WITH_EVENT = GENERATE_CONTRACT | GENERATE_EVENT;
  const unsigned char GENERATE_TESTER_WITH_EVENT = GENERATE_TESTER | GENERATE_EVENT;

  // reference doesn't require event

  grpc::string GetServices(const grpc::protobuf::FileDescriptor *file, const char flags);

}  // namespace grpc_contract_csharp_generator

#endif  // GRPC_INTERNAL_COMPILER_CSHARP_GENERATOR_H
