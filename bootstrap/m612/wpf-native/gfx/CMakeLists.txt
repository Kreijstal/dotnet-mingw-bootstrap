# CMake build for WPF's wpfgfx_cor3.dll (milcore) with mingw-w64.
# Replaces the core/dll/wpfgfx.vcxproj tree of dotnet/wpf.
# Lives in src/Microsoft.DotNet.Wpf/src/WpfGfx/.
cmake_minimum_required(VERSION 3.20)
project(WpfGfx LANGUAGES C CXX RC)

set(WPF_VERSION_SUFFIX "_cor3" CACHE STRING "WPF native DLL suffix")
set(WPF_VERSION "8.0.0" CACHE STRING "WPF version for the version resource")
option(WPFGFX_BUILD_DLL "Link wpfgfx_cor3.dll (otherwise only the static libraries)" ON)
string(REPLACE "." ";" _wpfver "${WPF_VERSION}")
list(GET _wpfver 0 WPF_VER_MAJOR)
list(GET _wpfver 1 WPF_VER_MINOR)
list(GET _wpfver 2 WPF_VER_PATCH)
set(WPFGFX_NAME "wpfgfx${WPF_VERSION_SUFFIX}")

set(GFX ${CMAKE_CURRENT_SOURCE_DIR})
set(SHARED ${GFX}/../Shared)
set(GEN ${CMAKE_CURRENT_BINARY_DIR}/gen)
file(MAKE_DIRECTORY ${GEN} ${GEN}/hw ${GEN}/av)
string(CONCAT _wpfver_header
    "#define WPF_VER_MAJOR ${WPF_VER_MAJOR}\n#define WPF_VER_MINOR ${WPF_VER_MINOR}\n"
    "#define WPF_VER_PATCH ${WPF_VER_PATCH}\n#define WPF_VER_STR \"${WPF_VERSION}\"\n")
file(CONFIGURE OUTPUT ${GEN}/wpfver.h CONTENT "${_wpfver_header}" @ONLY)

# eng/WpfArcadeSdk/tools/Wpf.Cpp.props (x64 release) + WpfGfx/Graphics.props
set(WPF_DEFS
    WIN32 _UNICODE UNICODE _WINDOWS STRICT
    _WIN32_WINNT=0x0A00 _WIN32_IE=0x0700 WINNT=1 WINVER=0x0A00
    SAL_NO_ATTRIBUTE_DECLARATIONS ARGITERATOR_SUPPORTED=0 NDEBUG
    _AMD64_ _LIB
    _ALLOW_KEYWORD_MACROS _WCTYPE_INLINE_DEFINED DIRECTXMATH
    # Graphics.props: override=__override, where __override is a no-op
    # annotation (WpfGfx writes 'override' on out-of-class definitions)
    override=__override __override=)

find_path(DIRECTXMATH_INCLUDE_DIR DirectXMath.h PATH_SUFFIXES directxmath REQUIRED)

set(GFX_INCLUDES
    ${GFX}/mingw
    ${GFX}/include
    ${GFX}/include/Generated
    ${GFX}/include/processed
    ${GFX}/shared/inc
    ${GFX}/common
    ${GFX}/core
    ${GFX}/common/DirectXLayer
    ${GFX}/common/DirectXLayer/interfaces
    ${GFX}/common/DirectXLayer/factories
    ${GFX}/common/DirectXLayer/D3DX9
    ${GFX}/shared/util/utillib
    ${SHARED}/inc
    ${SHARED}/Tracing/native
    ${GEN}
    ${DIRECTXMATH_INCLUDE_DIR})

# WpfGfx relies on MSVC behaviour (__if_exists, lookup into dependent base
# classes, goto past initializations, class-scope explicit specialization,
# ...), which only clang's -fms-compatibility mode provides in a mingw-w64
# build.  That mode normally also turns the compiler into "MSVC" for the
# headers; keep the GNU view for mingw-w64 and libstdc++/libc++ instead.
if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    message(FATAL_ERROR "WpfGfx needs clang (-fms-compatibility); "
                        "configure with -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++")
endif()
set(GFX_CXX_FLAGS
    "SHELL:-include ${GFX}/mingw/wpf_mingw_compat.h"
    "SHELL:-include ${GFX}/mingw/wpfgfx_mingw_compat.h"
    -fms-extensions -fms-compatibility -fms-compatibility-version=19.40
    -fdelayed-template-parsing
    # fxjit/Compiler/Coder86.h has enumerators named and/or/xor
    -fno-operator-names
    -fgnuc-version=4.2.1
    -U_MSC_VER -U_MSC_FULL_VER -U_MSC_BUILD -U_MSC_EXTENSIONS -U_MSVC_LANG
    -Wno-microsoft -Wno-macro-redefined -Wno-ignored-attributes
    -Wno-unknown-pragmas -Wno-pragma-pack -Wno-invalid-offsetof
    -Wno-unused-value -Wno-ignored-pragma-intrinsic -Wno-c++11-narrowing -Wno-register
    -Wno-address-of-temporary
    # clang silently drops class-scope __if_exists on dependent names
    -Werror=microsoft-exists)

function(wpfgfx_add_lib name)
    cmake_parse_arguments(L "" "" "INCLUDES;SOURCES" ${ARGN})
    add_library(${name} STATIC ${L_SOURCES})
    target_include_directories(${name} PRIVATE ${GFX}/mingw ${L_INCLUDES} ${GFX_INCLUDES})
    target_compile_definitions(${name} PRIVATE ${WPF_DEFS})
    target_compile_options(${name} PRIVATE ${GFX_CXX_FLAGS})
    set_property(GLOBAL APPEND PROPERTY WPFGFX_LIBS ${name})
endfunction()

# --- core/av: WPP trace headers ---------------------------------------------
# One <source>.tmh per av source, all of them forwarding to wpp_stub.h.
file(GLOB _av_sources RELATIVE ${GFX}/core/av ${GFX}/core/av/*.cpp)
foreach(_src IN LISTS _av_sources)
    get_filename_component(_base ${_src} NAME_WE)
    file(CONFIGURE OUTPUT ${GEN}/av/${_base}.tmh CONTENT "#include \"wpp_stub.h\"\n" @ONLY)
endforeach()

# OSVersionHelper from src/Shared
wpfgfx_add_lib(wpfgfx_osversionhelper
    INCLUDES ${SHARED}/OSVersionHelper
    SOURCES
        ${SHARED}/OSVersionHelper/osversionhelper.cpp
        ${SHARED}/OSVersionHelper/watsonreportinghelper.cpp)

wpfgfx_add_lib(wpfgfx_debuglib
    INCLUDES ${GFX}/shared/debug/DebugLib
    SOURCES
        ${GFX}/shared/debug/DebugLib/DebugLib.cxx)

wpfgfx_add_lib(wpfgfx_utillib
    INCLUDES ${GFX}/shared/util/UtilLib
    SOURCES
        ${GFX}/shared/util/UtilLib/pch.cxx
        ${GFX}/shared/util/UtilLib/Assert.cxx
        ${GFX}/shared/util/UtilLib/MemUtils.cxx
        ${GFX}/shared/util/UtilLib/Misc.cxx
        ${GFX}/shared/util/UtilLib/StrUtil.cxx
        ${GFX}/shared/util/UtilLib/Instrumentation.cxx
        ${GFX}/shared/util/UtilLib/list.cxx
        ${GFX}/shared/util/UtilLib/HiResTimer.cxx
        ${GFX}/shared/util/UtilLib/Registry.cxx
        ${GFX}/shared/util/UtilLib/DebugBreak.cpp)

wpfgfx_add_lib(wpfgfx_dllutil
    INCLUDES ${GFX}/shared/util/DllUtil
    SOURCES
        ${GFX}/shared/util/DllUtil/precomp.cxx
        ${GFX}/shared/util/DllUtil/dllmain.cxx
        ${GFX}/shared/util/DllUtil/dllmainimpl.cxx
        ${GFX}/shared/util/DllUtil/Data.cxx)

wpfgfx_add_lib(wpfgfx_dynamiccall
    INCLUDES ${GFX}/common/DynamicCall
    SOURCES
        ${GFX}/common/DynamicCall/DelayCall.cpp)

wpfgfx_add_lib(wpfgfx_shared
    INCLUDES ${GFX}/common/shared
    SOURCES
        ${GFX}/common/shared/precomp.cpp
        ${GFX}/common/shared/AssertEntry.cpp
        ${GFX}/common/shared/dynarrayimpl.cpp
        ${GFX}/common/shared/engine.cpp
        ${GFX}/common/shared/etwtrace.cpp
        ${GFX}/common/shared/GammaLUTs.cpp
        ${GFX}/common/shared/mem.cpp
        ${GFX}/common/shared/milcom.cpp
        ${GFX}/common/shared/MILRectF_WH.cpp
        ${GFX}/common/shared/MILRect.cpp
        ${GFX}/common/shared/pixelformatutils.cpp
        ${GFX}/common/shared/processorfeatures.cpp
        ${GFX}/common/shared/real.cpp
        ${GFX}/common/shared/RefCountBase.cpp
        ${GFX}/common/shared/ResourceCache.cpp
        ${GFX}/common/shared/rgnutils.cpp
        ${GFX}/common/shared/utc.cpp
        ${GFX}/common/shared/xmm.cpp
        ${GFX}/common/shared/utils.cpp)

wpfgfx_add_lib(wpfgfx_scanop
    INCLUDES ${GFX}/common/scanop
    SOURCES
        ${GFX}/common/scanop/precomp.cpp
        ${GFX}/common/scanop/bitmap.cpp
        ${GFX}/common/scanop/halftone.cpp
        ${GFX}/common/scanop/scanoperation.cpp
        ${GFX}/common/scanop/scanpipeline.cpp
        ${GFX}/common/scanop/scanpipelinebuilder.cpp
        ${GFX}/common/scanop/soalphamultiply.cpp
        ${GFX}/common/scanop/soblend.cpp
        ${GFX}/common/scanop/soblend_sse2.cpp
        ${GFX}/common/scanop/soconvert.cpp
        ${GFX}/common/scanop/socopy.cpp
        ${GFX}/common/scanop/sodither.cpp
        ${GFX}/common/scanop/sogammaconvert.cpp
        ${GFX}/common/scanop/soquantize.cpp
        ${GFX}/common/scanop/systembitmap.cpp
        ${GFX}/common/scanop/writeprotectedbitmap.cpp
        ${GFX}/common/scanop/bitmapwrappers.cpp)

wpfgfx_add_lib(wpfgfx_effects
    INCLUDES ${GFX}/common/effects
    SOURCES
        ${GFX}/common/effects/precomp.cpp
        ${GFX}/common/effects/effectlist.cpp)

wpfgfx_add_lib(wpfgfx_common
    INCLUDES ${GFX}/core/common
    SOURCES
        ${GFX}/core/common/precomp.cpp
        ${GFX}/core/common/AssertDllInUse.cpp
        ${GFX}/core/common/BaseMatrix.cpp
        ${GFX}/core/common/BufferDispenser.cpp
        ${GFX}/core/common/d3dloader.cpp
        ${GFX}/core/common/dwminterop.cpp
        ${GFX}/core/common/Gamma.cpp
        ${GFX}/core/common/display.cpp
        ${GFX}/core/common/engine.cpp
        ${GFX}/core/common/gradienttexture.cpp
        ${GFX}/core/common/matrix.cpp
        ${GFX}/core/common/matrix3x2.cpp
        ${GFX}/core/common/MatrixStack.cpp
        ${GFX}/core/common/memblock.cpp
        ${GFX}/core/common/MILMatrix.cpp
        ${GFX}/core/common/lazymem.cpp
        ${GFX}/core/common/RectUtils.cpp
        ${GFX}/core/common/ResourcePool.cpp
        ${GFX}/core/common/guidelinecollection.cpp
        ${GFX}/core/common/rtutils.cpp
        ${GFX}/core/common/utils.cpp
        ${GFX}/core/common/WatermarkStack.cpp
        ${GFX}/core/common/milcoredllentry.cpp
        ${GFX}/core/common/mildc.cpp
        ${GFX}/core/common/milboxf.cpp
        ${GFX}/core/common/d3dutils.cpp
        ${GFX}/core/common/3dutils.cpp
        ${GFX}/core/common/memreader.cpp
        ${GFX}/core/common/memblockreader.cpp
        ${GFX}/core/common/memwriter.cpp
        ${GFX}/core/common/slistutil.cpp
        ${GFX}/core/common/Dump.cpp
        ${GFX}/core/common/oscompat.cpp
        ${GFX}/core/common/Tier.cpp
        ${GFX}/core/common/log.cpp
        ${GFX}/core/common/ptrarray.cpp
        ${GFX}/core/common/ptrset.cpp
        ${GFX}/core/common/EnhancedContrastTable.cpp
        ${GFX}/core/common/exports.cpp
        ${GFX}/core/common/dwritefactory.cpp
        ${GFX}/core/common/renderoptions.cpp
        ${SHARED}/cpp/dwriteloader.cpp)

wpfgfx_add_lib(wpfgfx_util
    INCLUDES ${GFX}/core/control/util
    SOURCES
        ${GFX}/core/control/util/precomp.cpp
        ${GFX}/core/control/util/control.cpp)

wpfgfx_add_lib(wpfgfx_geometry
    INCLUDES ${GFX}/core/geometry
    SOURCES
        ${GFX}/core/geometry/precomp.cpp
        ${GFX}/core/geometry/utils.cpp
        ${GFX}/core/geometry/cpen.cpp
        ${GFX}/core/geometry/FigureBase.cpp
        ${GFX}/core/geometry/figure.cpp
        ${GFX}/core/geometry/FillTessellator.cpp
        ${GFX}/core/geometry/shapebase.cpp
        ${GFX}/core/geometry/shape.cpp
        ${GFX}/core/geometry/strokefigure.cpp
        ${GFX}/core/geometry/bezier.cpp
        ${GFX}/core/geometry/scanner.cpp
        ${GFX}/core/geometry/tessellate.cpp
        ${GFX}/core/geometry/Boolean.cpp
        ${GFX}/core/geometry/FigureTask.cpp
        ${GFX}/core/geometry/AnimationPath.cpp
        ${GFX}/core/geometry/area.cpp
        ${GFX}/core/geometry/ExactArithmetic.cpp
        ${GFX}/core/geometry/LineSegmentIntersection.cpp
        ${GFX}/core/geometry/compactshapes.cpp
        ${GFX}/core/geometry/BezierD.cpp
        ${GFX}/core/geometry/bezierflattener.cpp
        ${GFX}/core/geometry/StripClipper.cpp
        ${GFX}/core/geometry/AxisAlignedStripClipper.cpp)

wpfgfx_add_lib(wpfgfx_glyph
    INCLUDES ${GFX}/core/glyph
    SOURCES
        ${GFX}/core/glyph/precomp.cpp
        ${GFX}/core/glyph/glyphruncore.cpp
        ${GFX}/core/glyph/baseglyphpainter.cpp
        ${GFX}/core/glyph/BitmapDbgIO.cpp
        ${GFX}/core/glyph/perf.cpp)

wpfgfx_add_lib(wpfgfx_meta
    INCLUDES ${GFX}/core/meta
    SOURCES
        ${GFX}/core/meta/precomp.cpp
        ${GFX}/core/meta/desktophwndrt.cpp
        ${GFX}/core/meta/desktoprt.cpp
        ${GFX}/core/meta/dummyrt.cpp
        ${GFX}/core/meta/metabitmaprt.cpp
        ${GFX}/core/meta/metaiterator.cpp
        ${GFX}/core/meta/metart.cpp)

wpfgfx_add_lib(wpfgfx_sw
    INCLUDES ${GFX}/core/sw/swlib ${GFX}/core/sw
    SOURCES
        ${GFX}/core/sw/swlib/precomp.cpp
        # in the tree but its project (core/sw/bilinearspan/bilinearspan.vcxproj,
        # referenced by wpfgfx.vcxproj) is not; it uses swlib's precomp.hpp
        ${GFX}/core/sw/swlib/bilinearspan.cpp
        ${GFX}/core/sw/swlib/aacoverage.cpp
        ${GFX}/core/sw/swlib/aarasterizer.cpp
        ${GFX}/core/sw/swlib/boundsrt.cpp
        ${GFX}/core/sw/swlib/brushspan.cpp
        ${GFX}/core/sw/swlib/doublebufferedbitmap.cpp
        ${GFX}/core/sw/swlib/scanpipelinerender.cpp
        ${GFX}/core/sw/swlib/swbitmapcache.cpp
        ${GFX}/core/sw/swlib/swbitmapcolorsource.cpp
        ${GFX}/core/sw/swlib/swclip.cpp
        ${GFX}/core/sw/swlib/swhwndrt.cpp
        ${GFX}/core/sw/swlib/SwIntermediateRTCreator.cpp
        ${GFX}/core/sw/swlib/swpresentgdi.cpp
        ${GFX}/core/sw/swlib/swrast.cpp
        ${GFX}/core/sw/swlib/swsurfrt.cpp
        ${GFX}/core/sw/swlib/swglyphrun.cpp
        ${GFX}/core/sw/swlib/swglyphpainter.cpp
        ${GFX}/core/sw/swlib/renderingbuilder.cpp
        ${GFX}/core/sw/swlib/swinit.cpp)

wpfgfx_add_lib(wpfgfx_targets
    INCLUDES ${GFX}/core/targets
    SOURCES
        ${GFX}/core/targets/precomp.cpp
        ${GFX}/core/targets/BaseRT.cpp
        ${GFX}/core/targets/BaseSurfRT.cpp
        ${GFX}/core/targets/ShapeClipperForFEB.cpp)

wpfgfx_add_lib(wpfgfx_hw
    INCLUDES ${GFX}/core/hw ${GEN}/hw
    SOURCES
        ${GFX}/core/hw/precomp.cpp
        ${GFX}/core/hw/bitmapofdevicebitmaps.cpp
        ${GFX}/core/hw/d3ddevice.cpp
        ${GFX}/core/hw/d3ddevicemanager.cpp
        ${GFX}/core/hw/d3dgeometry.cpp
        ${GFX}/core/hw/d3dglyphbank.cpp
        ${GFX}/core/hw/d3dglyphpainter.cpp
        ${GFX}/core/hw/d3dglyphrun.cpp
        ${GFX}/core/hw/d3dlockabletexture.cpp
        ${GFX}/core/hw/d3dlog.cpp
        ${GFX}/core/hw/d3dregistry.cpp
        ${GFX}/core/hw/d3drenderstate.cpp
        ${GFX}/core/hw/d3dresource.cpp
        ${GFX}/core/hw/d3dstats.cpp
        ${GFX}/core/hw/d3dsurface.cpp
        ${GFX}/core/hw/d3dswapchain.cpp
        ${GFX}/core/hw/d3dswapchainwithswdc.cpp
        ${GFX}/core/hw/d3dtexture.cpp
        ${GFX}/core/hw/D3DTextureSurface.cpp
        ${GFX}/core/hw/d3dvidmemonlytexture.cpp
        ${GFX}/core/hw/gpumarker.cpp
        ${GFX}/core/hw/hw3dgeometrystreambuffer.cpp
        ${GFX}/core/hw/HwBitBltDeviceBitmapColorSource.cpp
        ${GFX}/core/hw/HwBitmapBrush.cpp
        ${GFX}/core/hw/HwBitmapCache.cpp
        ${GFX}/core/hw/hwbitmapcolorsource.cpp
        ${GFX}/core/hw/HwBoxColorSource.cpp
        ${GFX}/core/hw/HwBrushContext.cpp
        ${GFX}/core/hw/HwBrushPool.cpp
        ${GFX}/core/hw/HwColorComponentColorSource.cpp
        ${GFX}/core/hw/HwConstantColorSource.cpp
        ${GFX}/core/hw/hwdestinationtexture.cpp
        ${GFX}/core/hw/hwdestinationtexturepool.cpp
        ${GFX}/core/hw/hwdevicebitmapcolorsource.cpp
        ${GFX}/core/hw/hwdisplayrt.cpp
        ${GFX}/core/hw/hwffshaders.cpp
        ${GFX}/core/hw/HwHLSLShaderFragments.cpp
        ${GFX}/core/hw/hwhwndrt.cpp
        ${GFX}/core/hw/hwinit.cpp
        ${GFX}/core/hw/HwLightingColorSource.cpp
        ${GFX}/core/hw/HwLinearGradientBrush.cpp
        ${GFX}/core/hw/HwRadialGradientBrush.cpp
        ${GFX}/core/hw/hwlineargradientcolorsource.cpp
        ${GFX}/core/hw/HwRadialGradientColorSource.cpp
        ${GFX}/core/hw/hwpipeline.cpp
        ${GFX}/core/hw/hwpipelinebuilder.cpp
        ${GFX}/core/hw/hwrasterizer.cpp
        ${GFX}/core/hw/HwRenderStateManager.cpp
        ${GFX}/core/hw/hwshader.cpp
        ${GFX}/core/hw/HwShaderBuilder.cpp
        ${GFX}/core/hw/HwShaderCache.cpp
        ${GFX}/core/hw/HwShaderEffect.cpp
        ${GFX}/core/hw/hwshaderfragmenttohlslconverter.cpp
        ${GFX}/core/hw/HwShaderPipeline.cpp
        ${GFX}/core/hw/HwSolidBrush.cpp
        ${GFX}/core/hw/hwsolidcolortexturesource.cpp
        ${GFX}/core/hw/hwsolidcolortexturesourcepool.cpp
        ${GFX}/core/hw/HwStateTable.cpp
        ${GFX}/core/hw/hwsurfrt.cpp
        ${GFX}/core/hw/hwsurfrtdata.cpp
        ${GFX}/core/hw/hwsw3dfallback.cpp
        ${GFX}/core/hw/hwtexturedcolorsource.cpp
        ${GFX}/core/hw/HwTexturedColorSourceBrush.cpp
        ${GFX}/core/hw/hwtexturert.cpp
        ${GFX}/core/hw/HwUtils.cpp
        ${GFX}/core/hw/hwvertexbuffer.cpp
        ${GFX}/core/hw/HwVidMemTextureManager.cpp
        ${GFX}/core/hw/InteropDeviceBitmap.cpp
        ${GFX}/core/hw/swfallback.cpp
        ${GFX}/core/hw/waffler.cpp)

wpfgfx_add_lib(wpfgfx_resources
    INCLUDES ${GFX}/core/resources
    SOURCES
        ${GFX}/core/resources/precomp.cpp
        ${GFX}/core/resources/affinetransform3d.cpp
        ${GFX}/core/resources/ambientlight.cpp
        ${GFX}/core/resources/axisanglerotation3d.cpp
        ${GFX}/core/resources/bitmapres.cpp
        ${GFX}/core/resources/BitmapCacheBrush.cpp
        ${GFX}/core/resources/BitmapCacheMode.cpp
        ${GFX}/core/resources/BlurEffect.cpp
        ${GFX}/core/resources/brush.cpp
        ${GFX}/core/resources/BrushIntermediateCache.cpp
        ${GFX}/core/resources/BrushIntermediateRealizer.cpp
        ${GFX}/core/resources/BrushRealizer.cpp
        ${GFX}/core/resources/BrushTypeUtils.cpp
        ${GFX}/core/resources/CacheMode.cpp
        ${GFX}/core/resources/camera.cpp
        ${GFX}/core/resources/combinedgeometry.cpp
        ${GFX}/core/resources/CurrentValue.cpp
        ${GFX}/core/resources/d3dimage.cpp
        ${GFX}/core/resources/DashStyle.cpp
        ${GFX}/core/resources/DeviceAlignedIntermediateRealizer.cpp
        ${GFX}/core/resources/diffusematerial.cpp
        ${GFX}/core/resources/directionallight.cpp
        ${GFX}/core/resources/doublebufferedbitmapres.cpp
        ${GFX}/core/resources/drawingbrush.cpp
        ${GFX}/core/resources/drawinggroup.cpp
        ${GFX}/core/resources/drawingimage.cpp
        ${GFX}/core/resources/DropShadowEffect.cpp
        ${GFX}/core/resources/ellipsegeometry.cpp
        ${GFX}/core/resources/emissivematerial.cpp
        ${GFX}/core/resources/etwresource.cpp
        ${GFX}/core/resources/geometry.cpp
        ${GFX}/core/resources/geometry3d.cpp
        ${GFX}/core/resources/GeometryDrawing.cpp
        ${GFX}/core/resources/GeometryGroup.cpp
        ${GFX}/core/resources/geometrymodel3d.cpp
        ${GFX}/core/resources/GlyphRunDrawing.cpp
        ${GFX}/core/resources/GlyphRunGeometrySink.cpp
        ${GFX}/core/resources/glyphrunslave.cpp
        ${GFX}/core/resources/gradientbrush.cpp
        ${GFX}/core/resources/guidelinecollectionresource.cpp
        ${GFX}/core/resources/imagebrush.cpp
        ${GFX}/core/resources/ImageDrawing.cpp
        ${GFX}/core/resources/ImplicitInputBrush.cpp
        ${GFX}/core/resources/Effect.cpp
        ${GFX}/core/resources/light.cpp
        ${GFX}/core/resources/lineargradient.cpp
        ${GFX}/core/resources/linegeometry.cpp
        ${GFX}/core/resources/marshal_generated.cpp
        ${GFX}/core/resources/material.cpp
        ${GFX}/core/resources/materialgroup.cpp
        ${GFX}/core/resources/matrix.cpp
        ${GFX}/core/resources/matrixcamera.cpp
        ${GFX}/core/resources/matrixtransform3d.cpp
        ${GFX}/core/resources/meshgeometry3d.cpp
        ${GFX}/core/resources/model3d.cpp
        ${GFX}/core/resources/model3dgroup.cpp
        ${GFX}/core/resources/modelrenderwalker.cpp
        ${GFX}/core/resources/modelwalker.cpp
        ${GFX}/core/resources/node.cpp
        ${GFX}/core/resources/orthographiccamera.cpp
        ${GFX}/core/resources/pathgeometry.cpp
        ${GFX}/core/resources/PathGeometryWrapper.cpp
        ${GFX}/core/resources/pen.cpp
        ${GFX}/core/resources/perspectivecamera.cpp
        ${GFX}/core/resources/PixelShader.cpp
        ${GFX}/core/resources/pointlight.cpp
        ${GFX}/core/resources/prerenderwalker.cpp
        ${GFX}/core/resources/ProjectionCamera.cpp
        ${GFX}/core/resources/QuaternionRotation3D.cpp
        ${GFX}/core/resources/radialgradient.cpp
        ${GFX}/core/resources/rectanglegeometry.cpp
        ${GFX}/core/resources/renderdata.cpp
        ${GFX}/core/resources/renderdata_generated.cpp
        ${GFX}/core/resources/rotate.cpp
        ${GFX}/core/resources/rotatetransform3d.cpp
        ${GFX}/core/resources/rotation3d.cpp
        ${GFX}/core/resources/scale.cpp
        ${GFX}/core/resources/scaletransform3d.cpp
        ${GFX}/core/resources/ShaderEffect.cpp
        ${GFX}/core/resources/skew.cpp
        ${GFX}/core/resources/solidcolorbrush.cpp
        ${GFX}/core/resources/specularmaterial.cpp
        ${GFX}/core/resources/spotlight.cpp
        ${GFX}/core/resources/tilebrush.cpp
        ${GFX}/core/resources/TileBrushUtils.cpp
        ${GFX}/core/resources/transform.cpp
        ${GFX}/core/resources/transform3d.cpp
        ${GFX}/core/resources/transform3dgroup.cpp
        ${GFX}/core/resources/transformgroup.cpp
        ${GFX}/core/resources/translate.cpp
        ${GFX}/core/resources/translatetransform3d.cpp
        ${GFX}/core/resources/valueres.cpp
        ${GFX}/core/resources/videoslave.cpp
        ${GFX}/core/resources/VideoDrawing.cpp
        ${GFX}/core/resources/ViewportAlignedIntermediateRealizer.cpp
        ${GFX}/core/resources/viewport3dvisual.cpp
        ${GFX}/core/resources/visual3d.cpp
        ${GFX}/core/resources/visualbrush.cpp)

wpfgfx_add_lib(wpfgfx_uce
    INCLUDES ${GFX}/core/uce
    SOURCES
        ${GFX}/core/uce/precomp.cpp
        ${GFX}/core/uce/alphamaskwrapper.cpp
        ${GFX}/core/uce/apifunc.cpp
        ${GFX}/core/uce/clientchanneltables.cpp
        ${GFX}/core/uce/serverchanneltables.cpp
        ${GFX}/core/uce/clientchannel.cpp
        ${GFX}/core/uce/clipstack.cpp
        ${GFX}/core/uce/cmdbatch.cpp
        ${GFX}/core/uce/connection.cpp
        ${GFX}/core/uce/connectioncontext.cpp
        ${GFX}/core/uce/composition.cpp
        ${GFX}/core/uce/crossthreadcomposition.cpp
        ${GFX}/core/uce/dirtyregion.cpp
        ${GFX}/core/uce/DpiProvider.cpp
        ${GFX}/core/uce/drawingcontext.cpp
        ${GFX}/core/uce/generated_resource_factory.cpp
        ${GFX}/core/uce/geometry_api.cpp
        ${GFX}/core/uce/global.cpp
        ${GFX}/core/uce/glyphcacheslave.cpp
        ${GFX}/core/uce/graphwalker.cpp
        ${GFX}/core/uce/handletable.cpp
        ${GFX}/core/uce/htmaster.cpp
        ${GFX}/core/uce/htslave.cpp
        ${GFX}/core/uce/hwndtarget.cpp
        ${GFX}/core/uce/partition.cpp
        ${GFX}/core/uce/partitionmanager.cpp
        ${GFX}/core/uce/partitionthread.cpp
        ${GFX}/core/uce/precompctx.cpp
        ${GFX}/core/uce/Prerender3DContext.cpp
        ${GFX}/core/uce/printtarget.cpp
        ${GFX}/core/uce/render3dcontext.cpp
        ${GFX}/core/uce/RenderDataBounder.cpp
        ${GFX}/core/uce/rendertarget.cpp
        ${GFX}/core/uce/rendertargetmanager.cpp
        ${GFX}/core/uce/resslave.cpp
        ${GFX}/core/uce/rsapi.cpp
        ${GFX}/core/uce/samethreadcomposition.cpp
        ${GFX}/core/uce/schedulemanager.cpp
        ${GFX}/core/uce/serverchannel.cpp
        ${GFX}/core/uce/VisualCache.cpp
        ${GFX}/core/uce/VisualCacheManager.cpp
        ${GFX}/core/uce/VisualCacheSet.cpp
        ${GFX}/core/uce/vt_api.cpp)

wpfgfx_add_lib(wpfgfx_api
    INCLUDES ${GFX}/core/api
    SOURCES
        ${GFX}/core/api/precomp.cpp
        ${GFX}/core/api/api_base.cpp
        ${GFX}/core/api/api_basebrushes.cpp
        ${GFX}/core/api/api_brush.cpp
        ${GFX}/core/api/api_codecfactory.cpp
        ${GFX}/core/api/api_factory.cpp
        ${GFX}/core/api/api_mesh3d.cpp
        ${GFX}/core/api/api_renderstate.cpp
        ${GFX}/core/api/api_shader.cpp
        ${GFX}/core/api/api_utils.cpp
        ${GFX}/core/api/api_lights.cpp
        ${GFX}/core/api/api_lightdata.cpp
        ${GFX}/core/api/exports.cpp)

wpfgfx_add_lib(wpfgfx_av
    INCLUDES ${GFX}/core/av ${SHARED}/internal/multimedia/inc ${GEN}/av
    SOURCES
        ${GFX}/core/av/precomp.cpp
        ${GFX}/core/av/milav.cpp
        ${GFX}/core/av/avtrace.cpp
        ${GFX}/core/av/mediabuffer.cpp
        ${GFX}/core/av/dxvamanagerwrapper.cpp
        ${GFX}/core/av/WmpClientSite.cpp
        ${GFX}/core/av/Wmp11ClientSite.cpp
        ${GFX}/core/av/WmpPlayer.cpp
        ${GFX}/core/av/wmpstateengine.cpp
        ${GFX}/core/av/evrpresenter.cpp
        ${GFX}/core/av/WmpEventHandler.cpp
        ${GFX}/core/av/StateThread.cpp
        ${GFX}/core/av/fakepp.cpp
        ${GFX}/core/av/avloader.cpp
        ${GFX}/core/av/globals.cpp
        ${GFX}/core/av/util.cpp
        ${GFX}/core/av/mediaeventproxy.cpp
        ${GFX}/core/av/EvrFilterWrapper.cpp
        ${GFX}/core/av/samplequeue.cpp
        ${GFX}/core/av/playerstate.cpp
        ${GFX}/core/av/DummyPlayer.cpp
        ${GFX}/core/av/WmpStateEngineProxy.cpp
        ${GFX}/core/av/hwmediabuffer.cpp
        ${GFX}/core/av/swmediabuffer.cpp
        ${GFX}/core/av/activate.cpp
        ${GFX}/core/av/eventproxy.cpp
        ${GFX}/core/av/samplescheduler.cpp
        ${GFX}/core/av/MediaInstance.cpp
        ${GFX}/core/av/CompositionNotifier.cpp
        ${GFX}/core/av/ClockWrapper.cpp
        ${GFX}/core/av/PresenterWrapper.cpp
        ${GFX}/core/av/UniqueList.cpp
        ${GFX}/core/av/SharedState.cpp
        ${GFX}/core/av/UpdateState.cpp)

wpfgfx_add_lib(wpfgfx_fxjit_platform
    INCLUDES ${GFX}/core/fxjit/Platform ${GFX}/core/fxjit/Public ${GFX}/core/fxjit/Compiler
    SOURCES
        ${GFX}/core/fxjit/Platform/precomp.cpp
        ${GFX}/core/fxjit/Platform/Platform.cpp)

wpfgfx_add_lib(wpfgfx_fxjit_compiler
    INCLUDES ${GFX}/core/fxjit/Compiler ${GFX}/core/fxjit/Public ${GFX}/core/fxjit/Compiler
    SOURCES
        ${GFX}/core/fxjit/Compiler/precomp.cpp
        ${GFX}/core/fxjit/Compiler/Assemble.cpp
        ${GFX}/core/fxjit/Compiler/Bubbler.cpp
        ${GFX}/core/fxjit/Compiler/DependencyGraph.cpp
        ${GFX}/core/fxjit/Compiler/Dump.cpp
        ${GFX}/core/fxjit/Compiler/FlowControl.cpp
        ${GFX}/core/fxjit/Compiler/FlushMemory.cpp
        ${GFX}/core/fxjit/Compiler/Locator.cpp
        ${GFX}/core/fxjit/Compiler/Mapper.cpp
        ${GFX}/core/fxjit/Compiler/Operator.cpp
        ${GFX}/core/fxjit/Compiler/OperatorFlags.cpp
        ${GFX}/core/fxjit/Compiler/OperatorOpcodes.cpp
        ${GFX}/core/fxjit/Compiler/program.cpp
        ${GFX}/core/fxjit/Compiler/Shuffle.cpp
        ${GFX}/core/fxjit/Compiler/ShuffleRegs.cpp)

wpfgfx_add_lib(wpfgfx_fxjit_collector
    INCLUDES ${GFX}/core/fxjit/Collector ${GFX}/core/fxjit/Public ${GFX}/core/fxjit/Compiler
    SOURCES
        ${GFX}/core/fxjit/Collector/precomp.cpp
        ${GFX}/core/fxjit/Collector/Branch.cpp
        ${GFX}/core/fxjit/Collector/C_f32x1.cpp
        ${GFX}/core/fxjit/Collector/C_f32x4.cpp
        ${GFX}/core/fxjit/Collector/C_LazyVar.cpp
        ${GFX}/core/fxjit/Collector/C_s16x8.cpp
        ${GFX}/core/fxjit/Collector/C_s32x4.cpp
        ${GFX}/core/fxjit/Collector/C_u128x1.cpp
        ${GFX}/core/fxjit/Collector/C_u16x4.cpp
        ${GFX}/core/fxjit/Collector/C_u16x8.cpp
        ${GFX}/core/fxjit/Collector/C_u32.cpp
        ${GFX}/core/fxjit/Collector/C_u32x2.cpp
        ${GFX}/core/fxjit/Collector/C_u32x4.cpp
        ${GFX}/core/fxjit/Collector/C_u64.cpp
        ${GFX}/core/fxjit/Collector/C_u64x1.cpp
        ${GFX}/core/fxjit/Collector/C_u64x2.cpp
        ${GFX}/core/fxjit/Collector/C_u8x16.cpp
        ${GFX}/core/fxjit/Collector/C_u8x8.cpp
        ${GFX}/core/fxjit/Collector/jitteraccess.cpp
        ${GFX}/core/fxjit/Collector/MmValue.cpp
        ${GFX}/core/fxjit/Collector/PVOID.cpp
        ${GFX}/core/fxjit/Collector/P_f32x4.cpp
        ${GFX}/core/fxjit/Collector/XmmValue.cpp)

wpfgfx_add_lib(wpfgfx_fxjit_pixelshader
    INCLUDES ${GFX}/core/fxjit/PixelShader ${GFX}/core/fxjit/Public ${GFX}/core/fxjit/Compiler
    SOURCES
        ${GFX}/core/fxjit/PixelShader/precomp.cpp
        ${GFX}/core/fxjit/PixelShader/pshader.cpp
        ${GFX}/core/fxjit/PixelShader/pstrans.cpp
        ${GFX}/core/fxjit/PixelShader/rdpstrans.cpp
        ${GFX}/core/fxjit/PixelShader/shaderreg.cpp)


# --- wpfgfx_cor3.dll ----------------------------------------------------------
if(WPFGFX_BUILD_DLL)
    # --- core/hw shaders ----------------------------------------------------------
    # Text shaders: D3D9 assembly inside core/hw/shaders/*.fx, assembled by
    # fxasm.py (replaces the D3DX-generated core/hw/Shaders.rc).
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(_text_fx
        Text11A=${GFX}/core/hw/shaders/hlslTextShaders11A.fx
        Text11L=${GFX}/core/hw/shaders/hlslTextShaders11L.fx
        Text20A=${GFX}/core/hw/shaders/hlslTextShaders20A.fx
        Text20L=${GFX}/core/hw/shaders/hlslTextShaders20L.fx)
    add_custom_command(
        OUTPUT ${GEN}/hw/Shaders.rc
        COMMAND ${Python3_EXECUTABLE} ${GFX}/mingw/fxasm.py ${GEN}/hw/Shaders.rc ${_text_fx}
        DEPENDS ${GFX}/mingw/fxasm.py ${GFX}/core/hw/shaders/hlslTextShaders11A.fx
                ${GFX}/core/hw/shaders/hlslTextShaders11L.fx ${GFX}/core/hw/shaders/hlslTextShaders20A.fx
                ${GFX}/core/hw/shaders/hlslTextShaders20L.fx
        COMMENT "Assembling text pixel shaders")

    # Effect shaders: HLSL in core/hw/ShaderAssemblies/Source compiled with
    # vkd3d-shader (replaces fxc.exe in core/hw/ShaderAssemblies.targets).
    find_program(VKD3D_COMPILER vkd3d-compiler REQUIRED)
    set(_sa ${GFX}/core/hw/ShaderAssemblies)
    set(_sa_bin ${GEN}/hw/ShaderAssemblies/Binary)
    file(MAKE_DIRECTORY ${_sa_bin})
    set(_shader_outputs)
    foreach(_spec
            "ShaderEffectsVS.fx|vs_2_0|VS|ShaderEffectVS.vsbin"
            "ShaderEffectsVS.fx|vs_3_0|VS|ShaderEffectVS30.vsbin"
            "PassThroughShaderEffectPS.fx|ps_2_0|PS|PassThroughShaderEffectPS.psbin"
            "HorizontalBlur.fx|ps_2_0|PS|HorizontalBlur.psbin"
            "VerticalBlur.fx|ps_2_0|PS|VerticalBlur.psbin"
            "HorizontalBlurMulti.fx|ps_2_0|PS|HorizontalBlurMulti.psbin"
            "VerticalBlurMulti.fx|ps_2_0|PS|VerticalBlurMulti.psbin"
            "DropShadow.fx|ps_2_0|PS|DropShadow.psbin")
        string(REPLACE "|" ";" _f "${_spec}")
        list(GET _f 0 _src)
        list(GET _f 1 _profile)
        list(GET _f 2 _entry)
        list(GET _f 3 _out)
        add_custom_command(
            OUTPUT ${_sa_bin}/${_out}
            COMMAND ${VKD3D_COMPILER} -x hlsl -b d3dbc -p ${_profile} -e ${_entry}
                    -o ${_sa_bin}/${_out} ${_sa}/Source/${_src}
            DEPENDS ${_sa}/Source/${_src}
            COMMENT "vkd3d-compiler ${_profile} ${_src} -> ${_out}")
        list(APPEND _shader_outputs ${_sa_bin}/${_out})
    endforeach()
    configure_file(${_sa}/ResourceFileTemplate/EmbeddedShaders.RC
                   ${GEN}/hw/ShaderAssemblies/EmbeddedShaders.RC COPYONLY)
    configure_file(${_sa}/Shaders.h ${GEN}/hw/ShaderAssemblies/Shaders.h COPYONLY)
    configure_file(${GFX}/core/hw/hw.rc ${GEN}/hw/hw.rc COPYONLY)
    add_custom_target(wpfgfx_shaders DEPENDS ${GEN}/hw/Shaders.rc ${_shader_outputs})


    # milcore.rc without wpf-etw.rc: that one embeds mc.exe output
    # (MSG00001.bin, wpf-etwTEMP.BIN) checked into src/Shared/Tracing.
    file(CONFIGURE OUTPUT ${GEN}/milcore.rc CONTENT "#include <NativeVersion.rc>\n" @ONLY)
    configure_file(${GFX}/mingw/NativeVersion.rc ${GEN}/NativeVersion.rc COPYONLY)

    # Exports: link.exe matches the undecorated names in wpfgfx.def to the
    # (mostly C++) functions; gendef.py does that for GNU ld/lld and also
    # lists the symbols as --undefined so the archive members get linked.
    get_property(_libs GLOBAL PROPERTY WPFGFX_LIBS)
    set(_libs_files)
    foreach(_l IN LISTS _libs)
        list(APPEND _libs_files $<TARGET_FILE:${_l}>)
    endforeach()
    set(_def_out ${GEN}/wpfgfx.def)
    set(_def_rsp ${GEN}/wpfgfx_exports.rsp)
    find_program(WPFGFX_NM NAMES llvm-nm nm REQUIRED)
    add_custom_command(
        OUTPUT ${_def_out} ${_def_rsp}
        COMMAND ${Python3_EXECUTABLE} ${GFX}/mingw/gendef.py ${WPFGFX_NM}
                ${GFX}/core/dll/wpfgfx.def ${WPFGFX_NAME} ${_def_out} ${_def_rsp}
                ${_libs_files}
        DEPENDS ${GFX}/core/dll/wpfgfx.def ${GFX}/mingw/gendef.py ${_libs}
        COMMENT "Resolving wpfgfx.def exports")
    add_custom_target(wpfgfx_def DEPENDS ${_def_out} ${_def_rsp})

    add_library(wpfgfx SHARED
        ${GFX}/core/dll/precomp.cpp
        ${GFX}/core/dll/dllentry.cpp
        ${GFX}/mingw/dllcrt_alias.cpp
        ${GFX}/mingw/wpfgfx_mingw_shims.cpp
        ${GEN}/milcore.rc
        ${GEN}/hw/hw.rc
        ${_def_out})
    add_dependencies(wpfgfx wpfgfx_shaders wpfgfx_def)
    set_target_properties(wpfgfx PROPERTIES OUTPUT_NAME ${WPFGFX_NAME} PREFIX "")
    target_include_directories(wpfgfx PRIVATE ${GFX}/core/dll ${GFX_INCLUDES})
    target_compile_definitions(wpfgfx PRIVATE ${WPF_DEFS})
    target_compile_options(wpfgfx PRIVATE ${GFX_CXX_FLAGS})
    # wpfgfx.vcxproj: EntryPointSymbol _DllMainStartup (heap before CRT init)
    target_link_options(wpfgfx PRIVATE -Wl,--entry=_DllMainStartup "@${_def_rsp}")
    set_source_files_properties(${GEN}/hw/hw.rc PROPERTIES
        OBJECT_DEPENDS "${GEN}/hw/Shaders.rc;${_shader_outputs}"
        INCLUDE_DIRECTORIES "${GEN}/hw;${GFX}/core/hw")
    set_source_files_properties(${GEN}/milcore.rc PROPERTIES
        INCLUDE_DIRECTORIES "${GEN}")
    get_property(_libs GLOBAL PROPERTY WPFGFX_LIBS)
    target_link_libraries(wpfgfx PRIVATE
        "$<LINK_GROUP:RESCAN,${_libs}>"
        d3d9 d3dcompiler dwrite windowscodecs evr mfplat strmiids
        winmm user32 gdi32 ole32 oleaut32 uuid advapi32 rpcrt4 psapi ntdll
        comctl32 runtimeobject)
    install(TARGETS wpfgfx RUNTIME DESTINATION bin)
endif()
