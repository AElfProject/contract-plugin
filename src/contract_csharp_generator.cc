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

#include <cctype>
#include <map>
#include <sstream>
#include <vector>
#include <google/protobuf/stubs/logging.h>

#include "contract_csharp_generator.h"
#include "contract_csharp_generator_helpers.h"
#include "aelf_options.pb.h"

using google::protobuf::compiler::csharp::GetClassName;
using google::protobuf::compiler::csharp::GetFileNamespace;
using google::protobuf::compiler::csharp::GetReflectionClassName;
using google::protobuf::compiler::csharp::GetPropertyName;
using grpc::protobuf::Descriptor;
using grpc::protobuf::FileDescriptor;
using grpc::protobuf::MethodDescriptor;
using grpc::protobuf::ServiceDescriptor;
using grpc::protobuf::FieldDescriptor;
using grpc::protobuf::io::Printer;
using grpc::protobuf::io::StringOutputStream;
using grpc_generator::GetMethodType;
using grpc_generator::METHODTYPE_BIDI_STREAMING;
using grpc_generator::METHODTYPE_CLIENT_STREAMING;
using grpc_generator::METHODTYPE_NO_STREAMING;
using grpc_generator::METHODTYPE_SERVER_STREAMING;
using grpc_generator::MethodType;
using grpc_generator::StringReplace;
using std::map;
using std::vector;
using Services = std::vector<const ServiceDescriptor*>;
using Methods = std::vector<const MethodDescriptor*>;

namespace grpc_contract_csharp_generator {
namespace {

// This function is a massaged version of
// https://github.com/google/protobuf/blob/master/src/google/protobuf/compiler/csharp/csharp_doc_comment.cc
// Currently, we cannot easily reuse the functionality as
// google/protobuf/compiler/csharp/csharp_doc_comment.h is not a public header.
// TODO(jtattermusch): reuse the functionality from google/protobuf.
bool GenerateDocCommentBodyImpl(grpc::protobuf::io::Printer* printer,
                                grpc::protobuf::SourceLocation location) {
  grpc::string comments = location.leading_comments.empty()
                              ? location.trailing_comments
                              : location.leading_comments;
  if (comments.empty()) {
    return false;
  }
  // XML escaping... no need for apostrophes etc as the whole text is going to
  // be a child
  // node of a summary element, not part of an attribute.
  comments = grpc_generator::StringReplace(comments, "&", "&amp;", true);
  comments = grpc_generator::StringReplace(comments, "<", "&lt;", true);

  std::vector<grpc::string> lines;
  grpc_generator::Split(comments, '\n', &lines);
  // TODO: We really should work out which part to put in the summary and which
  // to put in the remarks...
  // but that needs to be part of a bigger effort to understand the markdown
  // better anyway.
  printer->Print("/// <summary>\n");
  bool last_was_empty = false;
  // We squash multiple blank lines down to one, and remove any trailing blank
  // lines. We need
  // to preserve the blank lines themselves, as this is relevant in the
  // markdown.
  // Note that we can't remove leading or trailing whitespace as *that's*
  // relevant in markdown too.
  // (We don't skip "just whitespace" lines, either.)
  for (std::vector<grpc::string>::iterator it = lines.begin(); it != lines.end(); ++it) {
    grpc::string line = *it;
    if (line.empty()) {
      last_was_empty = true;
    } else {
      if (last_was_empty) {
        printer->Print("///\n");
      }
      last_was_empty = false;
      printer->Print("///$line$\n", "line", *it);
    }
  }
  printer->Print("/// </summary>\n");
  return true;
}

template <typename DescriptorType>
bool GenerateDocCommentBody(grpc::protobuf::io::Printer* printer,
                            const DescriptorType* descriptor) {
  grpc::protobuf::SourceLocation location;
  if (!descriptor->GetSourceLocation(&location)) {
    return false;
  }
  return GenerateDocCommentBodyImpl(printer, location);
}

std::string GetServiceContainerClassName(const ServiceDescriptor* service) {
  return service->name()+"Container";
}

std::string GetServiceClassName(const ServiceDescriptor* service) {
  return service->name();
}

std::string GetServerClassName(const ServiceDescriptor* service) {
  return service->name() + "Base";
}

std::string GetTesterClassName(const ServiceDescriptor* service) {
  return service->name()+"Tester";
}

std::string GetReferenceClassName(const ServiceDescriptor* service) {
  return service->name()+"ReferenceState";
}

bool IsEventMessageType(const Descriptor* message){
  return message->options().GetExtension(aelf::is_event);
}

bool IsIndexedField(const FieldDescriptor* field){
  return field->options().GetExtension(aelf::is_indexed);
}

bool IsViewOnlyMethod(const MethodDescriptor* method) {
  return method->options().GetExtension(aelf::is_view);
}


int GetServiceBaseCount(const ServiceDescriptor* service){
  return service->options().ExtensionSize(aelf::base);
}

std::string GetServiceBase(const ServiceDescriptor* service, int index){
  return service->options().GetExtension(aelf::base, index);
}

void DepthFirstSearch(const ServiceDescriptor* service,
                      std::vector<const ServiceDescriptor*>* list,
                      std::set<const ServiceDescriptor*>* seen) {
  if (!seen->insert(service).second) {
    return;
  }

  const FileDescriptor* file = service->file();
  // Add all dependencies.
  for (int i = 0; i < file->dependency_count(); i++) {
    if(file->dependency(i)->service_count() == 0){
      continue;
    }
    if(file->dependency(i)->service_count() > 1){
      GOOGLE_LOG(ERROR) << file->dependency(i)->name() << ": File contains more than one service.";
    }
    DepthFirstSearch(file->dependency(i)->service(0), list, seen);
  }

  // Add this file.
  list->push_back(service);
}

void DepthFirstSearchForBase(const ServiceDescriptor* service,
                             std::vector<std::string>* list,
                             std::set<std::string>* seen,
                             std::map<std::string, const ServiceDescriptor*> all_services
) {
  if (!seen->insert(service->file()->name()).second) {
    return;
  }

  int baseCount = GetServiceBaseCount(service);
//  const FileDescriptor* file = service->file();
  // Add all dependencies.
  for (int i = 0; i < baseCount; i++) {
    std::string baseName = GetServiceBase(service, i);
    if(!all_services.count(baseName)){
      GOOGLE_LOG(ERROR) << "Can't find specified base " << baseName << ", did you forget to import it?";
    }
    const ServiceDescriptor* baseService = all_services[baseName];
    DepthFirstSearchForBase(baseService, list, seen, all_services);
  }

  // Add this file.
  list->push_back(service->file()->name());
}

Services GetFullService(const ServiceDescriptor* service){
  Services all_depended_services;
  std::set<const ServiceDescriptor*> seen;
  DepthFirstSearch(service, &all_depended_services, &seen);
  std::map<std::string, const ServiceDescriptor*> services;
  for(Services::iterator itr = all_depended_services.begin(); itr != all_depended_services.end(); ++itr){
    services[(*itr)->file()->name()]= *itr;
  }
  Services result;
  std::vector<std::string> bases;
  std::set<std::string> seenBases;
  DepthFirstSearchForBase(service, &bases, &seenBases, services);
  for(std::vector<std::string>::iterator itr = bases.begin(); itr != bases.end(); ++itr){
    result.push_back(services[*itr]);
  }
  return result;
}

Methods GetFullMethod(const ServiceDescriptor* service){
  Services services = GetFullService(service);
  Methods methods;
  for(Services::iterator itr = services.begin(); itr != services.end(); ++itr){
    for(int i = 0; i < (*itr)->method_count(); i++){
      methods.push_back((*itr)->method(i));
    }
  }
  return methods;
}

std::string GetCSharpMethodType(const MethodDescriptor* method) {
  if(IsViewOnlyMethod(method)) {
    return "aelf::MethodType.View";
  }
  return "aelf::MethodType.Action";
}
std::string GetServiceNameFieldName() { return "__ServiceName"; }
std::string GetStateTypeName(const ServiceDescriptor* service) { return service->options().GetExtension(aelf::csharp_state); }

std::string GetMarshallerFieldName(const Descriptor* message) {
  return "__Marshaller_" +
         grpc_generator::StringReplace(message->full_name(), ".", "_", true);
}

std::string GetMethodFieldName(const MethodDescriptor* method) {
  return "__Method_" + method->name();
}

std::string GetAccessLevel(char flags) {
  return flags & INTERNAL_ACCESS ? "internal" : "public";
}

bool NeedEvent(char flags) {
  return flags & GENERATE_EVENT;
}

bool NeedContract(char flags) {
  return flags & GENERATE_CONTRACT;
}

bool NeedTester(char flags) {
  return flags & GENERATE_TESTER;
}

bool NeedReference(char flags) {
  return flags & GENERATE_REFERENCE;
}

bool NeedContainer(char flags){
  return NeedContract(flags) | NeedTester(flags) | NeedReference(flags);
}

bool NeedOnlyEvent(char flags){
  return NeedEvent(flags) & !NeedContract(flags) & !NeedReference(flags) & !NeedTester(flags);
}

std::string GetMethodRequestParamServer(const MethodDescriptor* method) {
  switch (GetMethodType(method)) {
    case METHODTYPE_NO_STREAMING:
    case METHODTYPE_SERVER_STREAMING:
      return GetClassName(method->input_type()) + " input";
    case METHODTYPE_CLIENT_STREAMING:
    case METHODTYPE_BIDI_STREAMING:
      return "grpc::IAsyncStreamReader<" + GetClassName(method->input_type()) +
             "> requestStream";
  }
  GOOGLE_LOG(FATAL) << "Can't get here.";
  return "";
}

std::string GetMethodReturnTypeServer(const MethodDescriptor* method) {
  return GetClassName(method->output_type());
}

std::string GetMethodResponseStreamMaybe(const MethodDescriptor* method) {
  switch (GetMethodType(method)) {
    case METHODTYPE_NO_STREAMING:
    case METHODTYPE_CLIENT_STREAMING:
      return "";
    case METHODTYPE_SERVER_STREAMING:
    case METHODTYPE_BIDI_STREAMING:
      return ", grpc::IServerStreamWriter<" +
             GetClassName(method->output_type()) + "> responseStream";
  }
  GOOGLE_LOG(FATAL) << "Can't get here.";
  return "";
}

// Gets vector of all messages used as input or output types.
std::vector<const Descriptor*> GetUsedMessages(
    const ServiceDescriptor* service) {
  std::set<const Descriptor*> descriptor_set;
  std::vector<const Descriptor*>
      result;  // vector is to maintain stable ordering
  Methods methods = GetFullMethod(service);
  for (Methods::iterator itr = methods.begin(); itr != methods.end(); ++itr) {
    const MethodDescriptor* method = *itr;
    if (descriptor_set.find(method->input_type()) == descriptor_set.end()) {
      descriptor_set.insert(method->input_type());
      result.push_back(method->input_type());
    }
    if (descriptor_set.find(method->output_type()) == descriptor_set.end()) {
      descriptor_set.insert(method->output_type());
      result.push_back(method->output_type());
    }
  }
  return result;
}

void GenerateMarshallerFields(Printer* out, const ServiceDescriptor* service) {
  out->Print("#region Marshallers\n");
  std::vector<const Descriptor*> used_messages = GetUsedMessages(service);
  for (size_t i = 0; i < used_messages.size(); i++) {
    const Descriptor* message = used_messages[i];
    out->Print(
        "static readonly aelf::Marshaller<$type$> $fieldname$ = "
        "aelf::Marshallers.Create((arg) => "
        "global::Google.Protobuf.MessageExtensions.ToByteArray(arg), "
        "$type$.Parser.ParseFrom);\n",
        "fieldname", GetMarshallerFieldName(message), "type",
        GetClassName(message));
  }
  out->Print("#endregion\n");
  out->Print("\n");
}

void GenerateStaticMethodField(Printer* out, const MethodDescriptor* method) {
  out->Print(
      "static readonly aelf::Method<$request$, $response$> $fieldname$ = new "
      "aelf::Method<$request$, $response$>(\n",
      "fieldname", GetMethodFieldName(method), "request",
      GetClassName(method->input_type()), "response",
      GetClassName(method->output_type()));
  out->Indent();
  out->Indent();
  out->Print("$methodtype$,\n", "methodtype",
             GetCSharpMethodType(method));
  out->Print("$servicenamefield$,\n", "servicenamefield",
             GetServiceNameFieldName());
  out->Print("\"$methodname$\",\n", "methodname", method->name());
  out->Print("$requestmarshaller$,\n", "requestmarshaller",
             GetMarshallerFieldName(method->input_type()));
  out->Print("$responsemarshaller$);\n", "responsemarshaller",
             GetMarshallerFieldName(method->output_type()));
  out->Print("\n");
  out->Outdent();
  out->Outdent();
}

void GenerateServiceDescriptorProperty(Printer* out,
                                       const ServiceDescriptor* service) {
  std::ostringstream index;
  index << service->index();
  out->Print(
      "public static global::Google.Protobuf.Reflection.ServiceDescriptor "
      "Descriptor\n");
  out->Print("{\n");
  out->Print("  get { return $umbrella$.Descriptor.Services[$index$]; }\n",
             "umbrella", GetReflectionClassName(service->file()), "index",
             index.str());
  out->Print("}\n");
}

void GenerateAllServiceDescriptorsProperty(Printer* out,
                                           const ServiceDescriptor* service) {
  out->Print(
      "public static global::System.Collections.Generic.IReadOnlyList<global::Google.Protobuf.Reflection.ServiceDescriptor> Descriptors\n"
  );
  out->Print("{\n");
  {
    out->Indent();
    out->Print("get\n");
    out->Print("{\n");
    {
      out->Indent();
      out->Print("return new global::System.Collections.Generic.List<global::Google.Protobuf.Reflection.ServiceDescriptor>()\n");
      out->Print("{\n");
      {
        out->Indent();
        Services services = GetFullService(service);
        for(Services::iterator itr = services.begin(); itr != services.end(); ++itr){
          const ServiceDescriptor* svc = *itr;
          std::ostringstream index;
          index << svc->index();
          out->Print("$umbrella$.Descriptor.Services[$index$],\n",
                     "umbrella", GetReflectionClassName(svc->file()), "index",
                     index.str());
        }
        out->Outdent();
      }
      out->Print("};\n");
      out->Outdent();
    }
    out->Print("}\n");
    out->Outdent();
  }
  out->Print("}\n");
}

void GenerateContractBaseClass(Printer *out, const ServiceDescriptor *service) {
  
  out->Print(
      "/// <summary>Base class for the contract of "
      "$servicename$</summary>\n",
      "servicename", GetServiceClassName(service));
  out->Print("public abstract partial class $name$ : "
             "AElf.Sdk.CSharp.CSharpSmartContract<$statetype$>\n",
             "name", GetServerClassName(service),
             "statetype", GetStateTypeName(service));
  out->Print("{\n");
  out->Indent();
  Methods methods = GetFullMethod(service);
  for (Methods::iterator itr = methods.begin(); itr != methods.end(); ++itr) {
    const MethodDescriptor* method = *itr;
    out->Print(
        "public virtual $returntype$ "
        "$methodname$($request$$response_stream_maybe$)\n",
        "methodname", method->name(),
        "returntype", GetMethodReturnTypeServer(method),
        "request", GetMethodRequestParamServer(method),
        "response_stream_maybe", GetMethodResponseStreamMaybe(method));
    out->Print("{\n");
    out->Indent();
    out->Print(
        "throw new global::System.NotImplementedException();\n");
    out->Outdent();
    out->Print("}\n\n");
  }
  out->Outdent();
  out->Print("}\n");
  out->Print("\n");
}

void GenerateBindServiceMethod(Printer* out, const ServiceDescriptor* service) {
  out->Print(
      "public static aelf::ServerServiceDefinition BindService($implclass$ "
      "serviceImpl)\n",
      "implclass", GetServerClassName(service));
  out->Print("{\n");
  out->Indent();

  out->Print("return aelf::ServerServiceDefinition.CreateBuilder()");
  out->Indent();
  out->Indent();
  Methods methods = GetFullMethod(service);
  for (Methods::iterator itr = methods.begin(); itr != methods.end(); ++itr) {
    const MethodDescriptor* method = *itr;
    out->Print("\n.AddMethod($methodfield$, serviceImpl.$methodname$)",
               "methodfield", GetMethodFieldName(method), "methodname",
               method->name());
  }
  out->Print(".Build();\n");
  out->Outdent();
  out->Outdent();

  out->Outdent();
  out->Print("}\n");
  out->Print("\n");
}

void GenerateTesterClass(Printer* out, const ServiceDescriptor* service) {
  out->Print("public class $testername$ : aelf::ContractTesterBase\n",
             "testername", GetTesterClassName(service));
  out->Print("{\n");
  {
    out->Indent();
    Methods methods = GetFullMethod(service);
    for (Methods::iterator itr = methods.begin(); itr != methods.end(); ++itr) {
      const MethodDescriptor* method = *itr;
      out->Print(
          "public aelf::TestMethod<$request$, $response$> $fieldname$\n",
          "fieldname", method->name(),
          "request", GetClassName(method->input_type()),
          "response", GetClassName(method->output_type()));
      out->Print("{\n");
      {
        out->Indent();
        out->Print("get { return __factory.Create($fieldname$); }\n",
                   "fieldname", GetMethodFieldName(method));
        out->Outdent();
      }
      out->Print("}\n\n");
    }
    out->Outdent();
  }

  out->Print("}\n");
}


  void GenerateReferenceClass(Printer* out, const ServiceDescriptor* service, char flags) {

    // TODO: Maybe provide ContractReferenceState in options
    out->Print("public class $classname$ : global::AElf.Sdk.CSharp.State.ContractReferenceState\n",
               "classname", GetReferenceClassName(service));
    out->Print("{\n");
    {
      out->Indent();
      Methods methods = GetFullMethod(service);
      for (Methods::iterator itr = methods.begin(); itr != methods.end(); ++itr) {
        const MethodDescriptor* method = *itr;
        out->Print("$access_level$ global::AElf.Sdk.CSharp.State.MethodReference<$request$, $response$> $fieldname$ { get; set; }\n",
                   "access_level", GetAccessLevel(flags),
                   "fieldname", method->name(),
                   "request", GetClassName(method->input_type()),
                   "response", GetClassName(method->output_type()));
      }
      out->Outdent();
    }

    out->Print("}\n");
  }

bool HasEvent(const FileDescriptor* file){
  for(int i = 0; i < file->message_type_count(); i++){
    const Descriptor* message = file->message_type(i);
    if(IsEventMessageType(message))
      return true;
  }
  return false;
}

void GenerateEvent(Printer* out, const Descriptor* message, char flags){
  if(!IsEventMessageType(message)){
    return;
  }
  out->Print("$access_level$ partial class $classname$ : aelf::IEvent<$classname$>\n",
             "access_level", GetAccessLevel(flags),
             "classname", message->name());
  out->Print("{\n");
  {
    out->Indent();
    // GetIndexed
    out->Print("public global::System.Collections.Generic.IEnumerable<$classname$> GetIndexed()\n",
               "classname", message->name());
    out->Print("{\n");
    {
      out->Indent();
      for(int i = 0; i < message->field_count(); i++){
        const FieldDescriptor* field = message->field(i);
        if(IsIndexedField(field)){
          out->Print("yield return new $classname$\n", "classname", message->name());
          out->Print("{\n");
          {
            out->Indent();
            out->Print("$propertyname$ = $propertyname$\n", "propertyname", GetPropertyName(field));
            out->Outdent();
          }
          out->Print("};\n");
        }
      }
      out->Print("yield break;\n");
      out->Outdent();
    }
    out->Print("}\n\n");

    // GetNonIndexed
    out->Print("public $classname$ GetNonIndexed()\n", "classname", message->name());
    out->Print("{\n");
    {
      out->Indent();
      out->Print("return new $classname$\n", "classname", message->name());
      out->Print("{\n");
      {
        out->Indent();
        for(int i = 0; i < message->field_count(); i++){
          const FieldDescriptor* field = message->field(i);
          if(!IsIndexedField(field)){
            out->Print("$propertyname$ = $propertyname$,\n", "propertyname", GetPropertyName(field));
          }
        }
        out->Outdent();
      }
      out->Print("};\n");
      out->Outdent();
    }
    out->Print("}\n");
    out->Outdent();
  }

  out->Print("}\n\n");
}

void GenerateContainer(Printer *out, const ServiceDescriptor *service, char flags) {
  GenerateDocCommentBody(out, service);
  out->Print("$access_level$ static partial class $containername$\n",
             "access_level", GetAccessLevel(flags),
             "containername", GetServiceContainerClassName(service));
  out->Print("{\n");
  out->Indent();
  out->Print("static readonly string $servicenamefield$ = \"$servicename$\";\n",
             "servicenamefield", GetServiceNameFieldName(), "servicename",
             service->full_name());
  out->Print("\n");

  GenerateMarshallerFields(out, service);
  out->Print("#region Methods\n");
  Methods methods = GetFullMethod(service);
  for(Methods::iterator itr = methods.begin(); itr != methods.end(); ++itr) {
    GenerateStaticMethodField(out, *itr);
  }
  out->Print("#endregion\n");
  out->Print("\n");

  out->Print("#region Descriptors\n");
  GenerateServiceDescriptorProperty(out, service);
  out->Print("\n");
  GenerateAllServiceDescriptorsProperty(out, service);
  out->Print("#endregion\n");
  out->Print("\n");

  if (NeedContract(flags)) {
    GenerateContractBaseClass(out, service);
    GenerateBindServiceMethod(out, service);
  }

  if(NeedTester(flags)) {
    GenerateTesterClass(out, service);
  }

  if(NeedReference(flags)){
    GenerateReferenceClass(out, service, flags);
  }
  out->Outdent();
  out->Print("}\n");
}

}  // anonymous namespace

grpc::string GetServices(const FileDescriptor* file, char flags) {
  grpc::string output;
  {
    // Scope the output stream so it closes and finalizes output to the string.

    StringOutputStream output_stream(&output);
    Printer out(&output_stream, '$');

    // Don't write out any output if there no services, to avoid empty service
    // files being generated for proto files that don't declare any.
    if (file->service_count() == 0) {
      return output;
    }

    if(file->service_count() > 1){
      GOOGLE_LOG(ERROR) << file->name() << ": File contains more than one service.";
    }

    // Don't write out any output if there no event for event-only generation
    // scenario, this is usually for base contracts
    if(NeedOnlyEvent(flags) && !HasEvent(file)) {
      return output;
    }

    // Write out a file header.
    out.Print("// <auto-generated>\n");
    out.Print(
        "//     Generated by the protocol buffer compiler.  DO NOT EDIT!\n");
    out.Print("//     source: $filename$\n", "filename", file->name());
    out.Print("// </auto-generated>\n");

    // use C++ style as there are no file-level XML comments in .NET
    grpc::string leading_comments = GetCsharpComments(file, true);
    if (!leading_comments.empty()) {
      out.Print("// Original file comments:\n");
      out.PrintRaw(leading_comments.c_str());
    }

    out.Print("#pragma warning disable 0414, 1591\n");

    out.Print("#region Designer generated code\n");
    out.Print("\n");
    out.Print("using aelf = global::AElf.Types.CSharp;\n");
    out.Print("\n");

    grpc::string file_namespace = GetFileNamespace(file);
    if (file_namespace != "") {
      out.Print("namespace $namespace$ {\n", "namespace", file_namespace);
      out.Indent();
    }

    if(NeedEvent(flags)){
      // Events are not needed for contract reference
      out.Print("\n");
      out.Print("#region Events\n");
      for(int i = 0; i < file->message_type_count(); i++){
        const Descriptor* message = file->message_type(i);
        GenerateEvent(&out, message, flags);
      }
      out.Print("#endregion\n");
    }

    if(NeedContainer(flags)){
      for (int i = 0; i < file->service_count(); i++) {
        GenerateContainer(&out, file->service(i), flags);
      }
    }

    if (file_namespace != "") {
      out.Outdent();
      out.Print("}\n");
    }
    out.Print("#endregion\n");
    out.Print("\n");
  }
  return output;
}

}  // namespace grpc_contract_csharp_generator
