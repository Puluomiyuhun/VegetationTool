#pragma once
#include <string>
#include <vector>

// 自定义节点脚本执行器：运行用户 Lua 的 generate(ctx)，得到一批枝条参数。
// 脚本只产出数值(不接触引擎内存)，故安全；死循环由指令数上限的 debug hook 中断。
struct BranchSpec {
    float t         = 0.5f;   // 沿父级附着位置 [0,1]
    float azimuth   = 0.0f;   // 方位角(度): 绕父级轴向旋转
    float elevation = 50.0f;  // 仰角(度): 相对父级轴向抬起
    float length    = 1.0f;   // 枝条长度(绝对值)
    float radius    = 1.0f;   // start 半径倍数(× 父级附着点半径)
    float endRatio  = 0.25f;  // 末端半径 = start × endRatio
};

// 传给脚本 generate(ctx) 的只读上下文。
struct LuaCtx {
    int   count        = 6;
    float parentLen    = 4.0f;
    float parentRadius = 0.3f;
    int   depth        = 0;
    int   seed         = 8;
};

class LuaEngine {
public:
    // 执行 script 中的 generate(ctx)，把返回的枝条数组填入 out。
    // 成功返回 true(out 填充, err 清空)；失败返回 false(err 为错误信息, out 清空)。
    // maxBranches: 返回枝条数上限，超出截断；maxInstructions: 指令数上限，防死循环。
    static bool run(const std::string& script, const LuaCtx& ctx,
                    std::vector<BranchSpec>& out, std::string& err,
                    int maxBranches = 5000, long long maxInstructions = 5000000);
};
