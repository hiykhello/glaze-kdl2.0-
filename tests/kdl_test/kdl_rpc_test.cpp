// Glaze Library
// For the license information refer to glaze.hpp
#include "glaze/kdl.hpp"
#include "ut/ut.hpp"
using namespace ut;
struct rpc_arg
{
   int x{};
   std::string text{};
};
struct rpc_service
{
   int calls{};
   int sum(int x, int y)
   {
      ++calls;
      return x + y;
   }
   std::string echo(const std::string& text) const { return text; }
   rpc_arg transform(rpc_arg value)
   {
      ++value.x;
      return value;
   }
   int maybe(std::optional<rpc_arg> value) { return value ? value->x : -1; }
   void ping() { ++calls; }
};
template <>
struct glz::meta<rpc_service>
{
   static constexpr auto value = object("ping", &rpc_service::ping, "sum", &rpc_service::sum);
};
suite rpc_tests = [] {
   "native positional named structured requests"_test = [] {
      rpc_service service;
      glz::kdl::rpc_registry rpc;
      rpc.bind(service)
         .bind<&rpc_service::sum>("sum", service, {"x", "y"})
         .bind<&rpc_service::echo>("echo", service, {"text"})
         .bind<&rpc_service::transform>("transform", service, {"value"})
         .bind<&rpc_service::maybe>("maybe", service, {"value"});
      auto response = rpc.call("sum 2 3 id=\"r1\"");
      expect(bool(response));
      if (response) {
         auto value = glz::kdl::read_at<int>(*response, "/reply/result");
         expect(value && *value == 5);
         auto id = glz::kdl::read_at<std::string>(*response, "/reply/@id");
         expect(id && *id == "r1");
      }
      expect(bool(rpc.call("sum y=4 x=5")));
      auto structured = rpc.call("transform { value { x 4; text hello; } }");
      expect(bool(structured));
      if (structured) {
         auto v = glz::kdl::read_at<rpc_arg>(*structured, "/reply/result");
         expect(v && v->x == 5 && v->text == "hello");
      }
      expect(bool(rpc.call("echo text=hello")));
      auto nullable = rpc.call("maybe value=#null");
      expect(nullable && glz::kdl::read_at<int>(*nullable, "/reply/result").value_or(0) == -1);
      auto nested = rpc.call("maybe { value { x 8; text hello; } }");
      expect(nested && glz::kdl::read_at<int>(*nested, "/reply/result").value_or(0) == 8);
      expect(!rpc.call("maybe value=42"));
      const auto calls = service.calls;
      for (auto bad :
           {"sum 1", "sum 1 2 3", "sum 1 x=2", "sum x=1 x=2 y=3", "sum 1 2\nbad #invalid", "ping;ping", "missing"})
         expect(!rpc.call(bad));
      expect(service.calls == calls);
      expect(bool(rpc.call("ping")) && service.calls == calls + 1);
      auto error = glz::read_kdl<glz::kdl::document>(rpc.respond("missing"));
      expect(error && (*error)[0].tag == "error");
   };
};
int main() {}
