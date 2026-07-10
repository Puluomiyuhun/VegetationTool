#include "LuaEngine.h"
#include <sol/sol.hpp>
#include <random>
#include <cmath>

namespace {

// 指令数计数(防死循环): Lua debug hook 每执行若干指令递增, 超上限则抛错中断。
thread_local long long g_instrCount = 0;
thread_local long long g_instrLimit = 0;

void countHook(lua_State* L, lua_Debug* /*ar*/) {
    g_instrCount += 1000;   // hook 每 1000 条指令触发一次
    if (g_instrLimit > 0 && g_instrCount > g_instrLimit) {
        luaL_error(L, "脚本执行超时(疑似死循环): 指令数超过上限");
    }
}

// 从 Lua table 安全取 float 字段(缺失用默认)。
float getF(const sol::table& t, const char* key, float def) {
    sol::object o = t[key];
    if (o.is<double>()) return (float)o.as<double>();
    return def;
}

} // namespace

bool LuaEngine::run(const std::string& script, const LuaCtx& ctx,
                    std::vector<BranchSpec>& out, std::string& err,
                    int maxBranches, long long maxInstructions)
{
    out.clear();
    err.clear();

    try {
        sol::state lua;
        // 仅开放安全的标准库(数学/字符串/表/基础), 不开 io/os/package/debug 等。
        lua.open_libraries(sol::lib::base, sol::lib::math,
                           sol::lib::string, sol::lib::table);

        // 安装指令计数 hook(防死循环)
        g_instrCount = 0;
        g_instrLimit = maxInstructions;
        lua_sethook(lua.lua_state(), countHook, LUA_MASKCOUNT, 1000);

        // 确定性随机数(按种子), 供脚本 ctx.rand() 使用
        auto rng = std::make_shared<std::mt19937>((unsigned)ctx.seed);
        std::uniform_real_distribution<float> uni01(0.0f, 1.0f);

        // 构造 ctx 表
        sol::table c = lua.create_table();
        c["count"]        = ctx.count;
        c["parentLen"]    = ctx.parentLen;
        c["parentRadius"] = ctx.parentRadius;
        c["depth"]        = ctx.depth;
        c["seed"]         = ctx.seed;
        // rand(): [0,1); rand(a,b): [a,b)
        c["rand"] = [rng](sol::variadic_args va) -> double {
            std::uniform_real_distribution<double> d(0.0, 1.0);
            double r = d(*rng);
            if (va.size() >= 2) {
                double a = va[0].as<double>(), b = va[1].as<double>();
                return a + r * (b - a);
            }
            return r;
        };
        // noise(x): 平滑伪噪声 [-1,1], 供脚本做连续扰动
        c["noise"] = [](double x) -> double {
            double s = std::sin(x * 12.9898) * 43758.5453;
            return 2.0 * (s - std::floor(s)) - 1.0;
        };

        // 加载脚本
        sol::load_result loaded = lua.load(script);
        if (!loaded.valid()) {
            sol::error e = loaded;
            err = std::string("脚本编译错误: ") + e.what();
            return false;
        }
        sol::protected_function_result pfr = loaded();
        if (!pfr.valid()) {
            sol::error e = pfr;
            err = std::string("脚本运行错误: ") + e.what();
            return false;
        }

        // 取 generate 函数
        sol::protected_function gen = lua["generate"];
        if (!gen.valid()) {
            err = "脚本缺少 generate(ctx) 函数";
            return false;
        }

        sol::protected_function_result res = gen(c);
        if (!res.valid()) {
            sol::error e = res;
            err = std::string("generate() 执行错误: ") + e.what();
            return false;
        }

        sol::object ret = res;
        if (!ret.is<sol::table>()) {
            err = "generate() 必须返回一个枝条数组(table)";
            return false;
        }

        sol::table arr = ret.as<sol::table>();
        size_t n = arr.size();
        for (size_t i = 1; i <= n; ++i) {
            sol::object item = arr[i];
            if (!item.is<sol::table>()) continue;
            sol::table b = item.as<sol::table>();
            BranchSpec s;
            s.t         = getF(b, "t",         0.5f);
            s.azimuth   = getF(b, "azimuth",   0.0f);
            s.elevation = getF(b, "elevation", 50.0f);
            s.length    = getF(b, "length",    ctx.parentLen * 0.5f);
            s.radius    = getF(b, "radius",    1.0f);
            s.endRatio  = getF(b, "endRatio",  0.25f);
            out.push_back(s);
            if ((int)out.size() >= maxBranches) {
                err = "枝条数超过上限(" + std::to_string(maxBranches) + "), 已截断";
                break;   // 保留已生成的, 带非致命警告返回 true
            }
        }
        return true;   // 即便触发截断也算成功(err 作为警告)
    } catch (const std::exception& e) {
        err = std::string("脚本异常: ") + e.what();
        out.clear();
        return false;
    } catch (...) {
        err = "脚本未知异常";
        out.clear();
        return false;
    }
}
