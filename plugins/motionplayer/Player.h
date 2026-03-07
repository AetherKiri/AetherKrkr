//
// Created by LiDon on 2025/9/15.
//
#pragma once

namespace motion {

    class Player {
    public:
        // 游戏脚本 affinesourcemotion.tjs 会访问 Motion.Player.useD3D / enableD3D（Windows D3D 用）。
        // macOS 无 D3D，暴露为 false 避免脚本抛错。
        static bool getUseD3D() { return _useD3D; }
        static void setUseD3D(bool v) { _useD3D = v; }
        static bool getEnableD3D() { return _enableD3D; }
        static void setEnableD3D(bool v) { _enableD3D = v; }
    private:
        inline static bool _useD3D = false;
        inline static bool _enableD3D = false;
    };

} // namespace motion