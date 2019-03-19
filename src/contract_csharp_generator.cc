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

#include "contract_csharp_generator.h"
#include "contract_csharp_generator_helpers.h"
#include "aelf_options.pb.h"

using google::protobuf::compiler::csharp::GetClassName;
using google::protobuf::compiler::csharp::GetFileNamespace;
using google::protobuf::compiler::csharp::GetReflectionClassName;
using grpc::protobuf::Descriptor;
using grpc::protobuf::FileDescriptor;
using grpc::protobuf::MethodDescriptor;
using grpc::protobuf::ServiceDescriptor;
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
  for (std::vector<grpc::string>::iterator it = lines.begin();
       it != lines.end(); ++it) {
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

bool IsViewOnlyMethod(const MethodDescriptor* method) {
  return method->options().GetExtension(aelf::is_view);
}

std::string GetCSharpMethodType(const MethodDescriptor* method) {
  if(IsViewOnlyMethod(method)) {
    return "aelf::MethodType.View";
  }
  return "aelf::MethodType.Action";
}
std::string GetServiceNameFieldName() { return "__ServiceName"; }
std::string GetStateTypeName(const ServiceDescriptor* service) { return service->options().GetExtension(aelf::state_type); }

std::string GetMarshallerFieldName(const Descriptor* message) {
  return "__Marshaller_" +
         grpc_generator::StringReplace(message->full_name(), ".", "_", true);
}

std::string GetMethodFieldName(const MethodDescriptor* method) {
  return "__Method_" + method->name();
}

std::string GetAccessLevel(bool internal_access) {
  return internal_access ? "internal" : "public";
}

std::string GetMethodRequestParamServer(const MethodDescriptor* method) {
  switch (GetMethodType(method)) {
    case METHODTYPE_NO_STREAMING:
    case METHODTYPE_SERVER_STREAMING:
      return GetClassName(method->input_type()) + " request";
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
  for (int i = 0; i < service->method_count(); i++) {
    const MethodDescriptor* method = service->method(i);
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
  out->Print("/// <summary>Service descriptor</summary>\n");
  out->Print(
      "public static global::Google.Protobuf.Reflection.ServiceDescriptor "
      "Descriptor\n");
  out->Print("{\n");
  out->Print("  get { return $umbrella$.Descriptor.Services[$index$]; }\n",
             "umbrella", GetReflectionClassName(service->file()), "index",
             index.str());
  out->Print("}\n");
  out->Print("\n");
}

void GenerateServerClass(Printer* out, const ServiceDescriptor* service) {
  
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
  for (int i = 0; i < service->method_count(); i++) {
    const MethodDescriptor* method = service->method(i);
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
  for (int i = 0; i < service->method_count(); i++) {
    const MethodDescriptor* method = service->method(i);
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

void GenerateService(Printer* out, const ServiceDescriptor* service,
                     bool generate_client, bool generate_server,
                     bool internal_access) {
  GenerateDocCommentBody(out, service);
  out->Print("$access_level$ static partial class $containername$\n",
             "access_level", GetAccessLevel(internal_access),
             "containername", GetServiceContainerClassName(service));
  out->Print("{\n");
  out->Indent();
  out->Print("static readonly string $servicenamefield$ = \"$servicename$\";\n",
             "servicenamefield", GetServiceNameFieldName(), "servicename",
             service->full_name());
  out->Print("\n");

  GenerateMarshallerFields(out, service);
  for (int i = 0; i < service->method_count(); i++) {
    GenerateStaticMethodField(out, service->method(i));
  }
  GenerateServiceDescriptorProperty(out, service);

  if (generate_server) {
    GenerateServerClass(out, service);
  }
  if (generate_server) {
    GenerateBindServiceMethod(out, service);
  }

  out->Outdent();
  out->Print("}\n");
}

}  // anonymous namespace

grpc::string GetServices(const FileDescriptor* file, bool generate_client,
                         bool generate_server, bool internal_access) {
  grpc::string output;
  {
    // return file->DebugString();
    // Scope the output stream so it closes and finalizes output to the string.

    StringOutputStream output_stream(&output);
    Printer out(&output_stream, '$');

    // Don't write out any output if there no services, to avoid empty service
    // files being generated for proto files that don't declare any.
    if (file->service_count() == 0) {
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
    for (int i = 0; i < file->service_count(); i++) {
      GenerateService(&out, file->service(i), generate_client, generate_server,
                      internal_access);
    }
    if (file_namespace != "") {
      out.Outdent();
      out.Print("}\n");
    }
    out.Print("#endregion\n");
  }
  return output;
}

}  // namespace grpc_contract_csharp_generator
