cmake_minimum_required(VERSION 3.27)

# Build against both ARM and x86_64 architectures for Apple targets. Both executables are packaged
# together in a so-called "fat" binary. This must be set before the first call to either `project`
# or `enable_language`.
if(DISABLE_FAT_BINARY)
    set(CMAKE_OSX_ARCHITECTURES "arm64")
else()
    set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64")
endif()

# Lowest version of the MacOS SDK that we can compile our code against, currently.
set(CMAKE_OSX_DEPLOYMENT_TARGET "13.3")

# Project setup
project(Realmz
    VERSION 8.1.0
    DESCRIPTION "Unofficial remaster of the classic turn-based RPG"
    LANGUAGES C CXX
)
# Enable Objective-C only on macOS (needed for MenuController.mm)
if(APPLE)
    enable_language(OBJC)
endif()

set(REALMZ_PRODUCT_NAME "Realmz Remastered — Unofficial")
set(REALMZ_BUNDLE_IDENTIFIER "org.realmz-remastered.unofficial")
set(PROJECT_COPYRIGHT "Realmz © 1994 Tim Phillips; unofficial fork modifications © contributors")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_XCODE_ATTRIBUTE_DEBUG_INFORMATION_FORMAT "dwarf")
# TODO(martin): We should eventually get everything to built without warnings and enable these flags.
# if (MSVC)
#     add_compile_options(/W4 /WX)
# else()
#     add_compile_options(-Wall -Wextra -Werror)
# endif()

option(REALMZ_CORE_ONLY
    "Configure only dependency-free remaster core libraries and tests"
    OFF)
option(REALMZ_BUILD_CORE_TESTS
    "Build dependency-free presentation, asset, and user-data tests"
    ON)
option(REALMZ_ENABLE_CORE_SANITIZERS
    "Enable AddressSanitizer and UndefinedBehaviorSanitizer for core tests"
    OFF)
option(REALMZ_ENABLE_APP_SANITIZERS
    "Instrument non-Release application code with AddressSanitizer and UndefinedBehaviorSanitizer"
    ON)
option(REALMZ_COPY_DEVELOPMENT_RESOURCES
    "Copy phase-one data beside the raw build-tree executable"
    OFF)

function(realmz_configure_core_target target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX)
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
        if(REALMZ_ENABLE_CORE_SANITIZERS)
            target_compile_options(${target} PRIVATE
                -fsanitize=address,undefined -fno-omit-frame-pointer)
            get_target_property(target_type ${target} TYPE)
            if(NOT target_type STREQUAL "STATIC_LIBRARY")
                target_link_options(${target} PRIVATE
                    -fsanitize=address,undefined)
            endif()
        endif()
    endif()
endfunction()

set(PRESENTATION_CORE_SOURCES
    src/presentation/AdaptiveShell.cpp
    src/presentation/DrawerControlLayout.cpp
    src/presentation/LegacyCommandBridge.cpp
    src/presentation/PartyRailControlLayout.cpp
    src/presentation/PartyRailLayout.cpp
    src/presentation/PartyRailModel.cpp
    src/presentation/PresentationHost.cpp
    src/presentation/RemasteredInputMapper.cpp
    src/presentation/ResponsiveLayout.cpp
    src/presentation/RuntimeLegacyCommandBridge.cpp
    src/presentation/ShellControlLayout.cpp
    src/presentation/ShellKeyboardInteraction.cpp
    src/presentation/UIPrimitives.cpp
)

add_library(realmz_presentation_core STATIC ${PRESENTATION_CORE_SOURCES})
target_include_directories(realmz_presentation_core PUBLIC src)
realmz_configure_core_target(realmz_presentation_core)

add_library(realmz_asset_core STATIC
    src/remaster/assets/AssetManifest.cpp
    src/remaster/assets/AssetResolver.cpp
    src/remaster/assets/PayloadDigest.cpp
    src/remaster/assets/ResourceSelectionHook.cpp
)
target_include_directories(realmz_asset_core PUBLIC src)
realmz_configure_core_target(realmz_asset_core)

add_library(realmz_userdata_core STATIC
    src/userdata/UserDataPathPolicy.cpp
    src/userdata/UserDataMigration.cpp
)
target_include_directories(realmz_userdata_core PUBLIC src)
realmz_configure_core_target(realmz_userdata_core)

if(REALMZ_BUILD_CORE_TESTS)
    enable_testing()
    add_executable(PresentationCoreTest src/tests/PresentationCoreTest.cpp)
    target_link_libraries(PresentationCoreTest PRIVATE realmz_presentation_core)
    realmz_configure_core_target(PresentationCoreTest)
    add_test(NAME PresentationCoreTest COMMAND PresentationCoreTest)

    add_executable(AdaptiveShellTest src/tests/AdaptiveShellTest.cpp)
    target_link_libraries(AdaptiveShellTest PRIVATE realmz_presentation_core)
    realmz_configure_core_target(AdaptiveShellTest)
    add_test(NAME AdaptiveShellTest COMMAND AdaptiveShellTest)

    add_executable(RemasteredInputMapperTest
        src/tests/RemasteredInputMapperTest.cpp)
    target_link_libraries(RemasteredInputMapperTest PRIVATE
        realmz_presentation_core)
    realmz_configure_core_target(RemasteredInputMapperTest)
    add_test(NAME RemasteredInputMapperTest
        COMMAND RemasteredInputMapperTest)

    add_executable(PartyRailModelTest src/tests/PartyRailModelTest.cpp)
    target_link_libraries(PartyRailModelTest PRIVATE realmz_presentation_core)
    realmz_configure_core_target(PartyRailModelTest)
    add_test(NAME PartyRailModelTest COMMAND PartyRailModelTest)

    add_executable(PartyRailLayoutTest src/tests/PartyRailLayoutTest.cpp)
    target_link_libraries(PartyRailLayoutTest PRIVATE realmz_presentation_core)
    realmz_configure_core_target(PartyRailLayoutTest)
    add_test(NAME PartyRailLayoutTest COMMAND PartyRailLayoutTest)

    add_executable(PartyRailControlLayoutTest
        src/tests/PartyRailControlLayoutTest.cpp)
    target_link_libraries(PartyRailControlLayoutTest PRIVATE
        realmz_presentation_core)
    realmz_configure_core_target(PartyRailControlLayoutTest)
    add_test(NAME PartyRailControlLayoutTest
        COMMAND PartyRailControlLayoutTest)

    add_executable(DrawerControlLayoutTest
        src/tests/DrawerControlLayoutTest.cpp)
    target_link_libraries(DrawerControlLayoutTest PRIVATE
        realmz_presentation_core)
    realmz_configure_core_target(DrawerControlLayoutTest)
    add_test(NAME DrawerControlLayoutTest
        COMMAND DrawerControlLayoutTest)

    add_executable(RuntimeLegacyCommandBridgeTest
        src/tests/RuntimeLegacyCommandBridgeTest.cpp)
    target_link_libraries(RuntimeLegacyCommandBridgeTest PRIVATE
        realmz_presentation_core)
    realmz_configure_core_target(RuntimeLegacyCommandBridgeTest)
    add_test(NAME RuntimeLegacyCommandBridgeTest
        COMMAND RuntimeLegacyCommandBridgeTest)

    add_executable(ShellControlLayoutTest
        src/tests/ShellControlLayoutTest.cpp)
    target_link_libraries(ShellControlLayoutTest PRIVATE
        realmz_presentation_core)
    realmz_configure_core_target(ShellControlLayoutTest)
    add_test(NAME ShellControlLayoutTest COMMAND ShellControlLayoutTest)

    add_executable(ShellKeyboardInteractionTest
        src/tests/ShellKeyboardInteractionTest.cpp)
    target_link_libraries(ShellKeyboardInteractionTest PRIVATE
        realmz_presentation_core)
    realmz_configure_core_target(ShellKeyboardInteractionTest)
    add_test(NAME ShellKeyboardInteractionTest
        COMMAND ShellKeyboardInteractionTest)

    add_executable(SemanticInputBoundaryTest
        src/tests/SemanticInputBoundaryTest.cpp
        src/presentation/SemanticInputBoundary.cpp)
    target_link_libraries(SemanticInputBoundaryTest PRIVATE
        realmz_presentation_core)
    realmz_configure_core_target(SemanticInputBoundaryTest)
    add_test(NAME SemanticInputBoundaryTest
        COMMAND SemanticInputBoundaryTest)

    add_executable(SemanticCombatLegacyAdapterTest
        src/tests/SemanticCombatLegacyAdapterTest.cpp
        src/presentation/SemanticInputBoundary.cpp
        src/presentation/LegacyGameSnapshotSource.cpp
        src/presentation/LegacyPresentationContext.c)
    target_link_libraries(SemanticCombatLegacyAdapterTest PRIVATE
        realmz_presentation_core)
    target_include_directories(SemanticCombatLegacyAdapterTest PRIVATE src)
    set_target_properties(SemanticCombatLegacyAdapterTest PROPERTIES
        C_STANDARD 99)
    realmz_configure_core_target(SemanticCombatLegacyAdapterTest)
    add_test(NAME SemanticCombatLegacyAdapterTest
        COMMAND SemanticCombatLegacyAdapterTest)

    add_executable(SemanticTopLevelLoopContractTest
        src/tests/SemanticTopLevelLoopContractTest.cpp)
    realmz_configure_core_target(SemanticTopLevelLoopContractTest)
    add_test(NAME SemanticTopLevelLoopContractTest
        COMMAND SemanticTopLevelLoopContractTest ${CMAKE_SOURCE_DIR})

    add_executable(ExplorationActionEquivalenceTest
        tests/semantic/ExplorationActionEquivalenceTest.cpp)
    target_link_libraries(ExplorationActionEquivalenceTest PRIVATE
        realmz_presentation_core)
    realmz_configure_core_target(ExplorationActionEquivalenceTest)
    add_test(NAME ExplorationActionEquivalenceTest
        COMMAND ExplorationActionEquivalenceTest)

    add_executable(CombatActionEquivalenceTest
        tests/semantic/CombatActionEquivalenceTest.cpp
        src/presentation/SemanticInputBoundary.cpp)
    target_link_libraries(CombatActionEquivalenceTest PRIVATE
        realmz_presentation_core)
    realmz_configure_core_target(CombatActionEquivalenceTest)
    add_test(NAME CombatActionEquivalenceTest
        COMMAND CombatActionEquivalenceTest)

    add_executable(ShellKeyboardInteractionContractTest
        tests/semantic/shell_keyboard/ShellKeyboardInteractionContractTest.cpp)
    target_link_libraries(ShellKeyboardInteractionContractTest PRIVATE
        realmz_presentation_core)
    realmz_configure_core_target(ShellKeyboardInteractionContractTest)
    add_test(NAME ShellKeyboardInteractionContractTest
        COMMAND ShellKeyboardInteractionContractTest)

    add_executable(LegacyPresentationContextTest
        src/tests/LegacyPresentationContextTest.cpp
        src/presentation/LegacyPresentationContext.c)
    target_include_directories(LegacyPresentationContextTest PRIVATE src)
    set_target_properties(LegacyPresentationContextTest PROPERTIES C_STANDARD 99)
    realmz_configure_core_target(LegacyPresentationContextTest)
    add_test(NAME LegacyPresentationContextTest
        COMMAND LegacyPresentationContextTest)

    add_executable(LegacyPartySelectionTest
        src/tests/LegacyPartySelectionTest.cpp
        src/presentation/LegacyPartySelection.c)
    target_include_directories(LegacyPartySelectionTest PRIVATE src)
    set_target_properties(LegacyPartySelectionTest PROPERTIES C_STANDARD 99)
    realmz_configure_core_target(LegacyPartySelectionTest)
    add_test(NAME LegacyPartySelectionTest
        COMMAND LegacyPartySelectionTest)

    add_executable(AssetResolverTest src/tests/AssetResolverTest.cpp)
    target_link_libraries(AssetResolverTest PRIVATE realmz_asset_core)
    realmz_configure_core_target(AssetResolverTest)
    add_test(NAME AssetResolverTest
        COMMAND AssetResolverTest ${CMAKE_SOURCE_DIR})

    add_executable(ResourceSelectionHookTest
        src/tests/ResourceSelectionHookTest.cpp)
    target_link_libraries(ResourceSelectionHookTest PRIVATE realmz_asset_core)
    realmz_configure_core_target(ResourceSelectionHookTest)
    add_test(NAME ResourceSelectionHookTest
        COMMAND ResourceSelectionHookTest ${CMAKE_SOURCE_DIR})

    add_executable(RuntimePresentationContractTest
        src/tests/RuntimePresentationContractTest.cpp)
    target_link_libraries(RuntimePresentationContractTest PRIVATE
        realmz_presentation_core
        realmz_asset_core)
    realmz_configure_core_target(RuntimePresentationContractTest)
    add_test(NAME RuntimePresentationContractTest
        COMMAND RuntimePresentationContractTest ${CMAKE_SOURCE_DIR})

    add_executable(LegacyGameSnapshotSourceTest
        src/tests/LegacyGameSnapshotSourceTest.cpp
        src/presentation/LegacyGameSnapshotSource.cpp)
    target_include_directories(LegacyGameSnapshotSourceTest PRIVATE src)
    realmz_configure_core_target(LegacyGameSnapshotSourceTest)
    add_test(NAME LegacyGameSnapshotSourceTest
        COMMAND LegacyGameSnapshotSourceTest)

    add_executable(UserDataMigrationTest src/tests/UserDataMigrationTest.cpp)
    target_link_libraries(UserDataMigrationTest PRIVATE realmz_userdata_core)
    realmz_configure_core_target(UserDataMigrationTest)
    add_test(NAME UserDataMigrationTest COMMAND UserDataMigrationTest)

    add_executable(UserDataPathPolicyTest src/tests/UserDataPathPolicyTest.cpp)
    target_link_libraries(UserDataPathPolicyTest PRIVATE realmz_userdata_core)
    realmz_configure_core_target(UserDataPathPolicyTest)
    add_test(NAME UserDataPathPolicyTest COMMAND UserDataPathPolicyTest)
endif()

if(REALMZ_CORE_ONLY)
    return()
endif()

# set the output directory for built objects.
# This makes sure that the dynamic library goes into the build directory automatically.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/")

# Library search
find_package(phosg REQUIRED)
message(STATUS "CMake found phosg at: ${phosg_DIR}")
find_package(resource_file REQUIRED)
message(STATUS "CMake found resource_file at: ${resource_file_DIR}")
find_package(ZLIB REQUIRED)
message(STATUS "CMake found zlib at: ${zlib_DIR}")

if(REALMZ_BUILD_CORE_TESTS)
    add_executable(ResourceForkSelectionIntegrationTest
        src/tests/ResourceForkSelectionIntegrationTest.cpp)
    target_link_libraries(ResourceForkSelectionIntegrationTest PRIVATE
        realmz_asset_core
        resource_file::resource_file
        phosg::phosg)
    realmz_configure_core_target(ResourceForkSelectionIntegrationTest)
    add_test(NAME ResourceForkSelectionIntegrationTest
        COMMAND ResourceForkSelectionIntegrationTest ${CMAKE_SOURCE_DIR})
endif()

# For cross-compilation to Windows
if(WIN32)
    link_directories($ENV{HOME}/mingw-install/lib ${CMAKE_CURRENT_BINARY_DIR}/vendored/SDL_ttf/external/freetype)
    if(DEFINED WINDOWS_ENV)
        link_directories(${WINDOWS_ENV}/lib)
    endif()
endif()

# Dependencies
add_subdirectory(vendored/SDL EXCLUDE_FROM_ALL)

# Use the pinned FreeType source that is already nested under SDL_ttf. Realmz's
# current text is single-script and does not require HarfBuzz or SVG glyphs;
# excluding host-package-manager copies keeps both universal slices and the
# macOS 13.3 deployment target reproducible.
set(SDLTTF_VENDORED ON CACHE BOOL "Use pinned SDL_ttf dependencies")
set(SDLTTF_HARFBUZZ OFF CACHE BOOL "Realmz does not require text shaping")
set(SDLTTF_PLUTOSVG OFF CACHE BOOL "Realmz does not require SVG glyphs")
add_subdirectory(vendored/SDL_ttf EXCLUDE_FROM_ALL)

# Remastered raster overrides are lossless PNGs. Keep SDL_image's build to the
# one format the fork actually consumes so vendored transitive codecs do not
# silently expand the dependency and architecture surface. Command-line cache
# values still take precedence over these first-configure defaults.
set(SDLIMAGE_VENDORED ON CACHE BOOL "Use pinned SDL_image dependencies")
set(SDLIMAGE_AVIF OFF CACHE BOOL "Realmz does not load AVIF")
set(SDLIMAGE_JPG OFF CACHE BOOL "Realmz does not load JPEG")
set(SDLIMAGE_JXL OFF CACHE BOOL "Realmz does not load JPEG XL")
set(SDLIMAGE_TIF OFF CACHE BOOL "Realmz does not load TIFF")
set(SDLIMAGE_WEBP OFF CACHE BOOL "Realmz does not load WebP")
set(SDLIMAGE_PNG_LIBPNG OFF CACHE BOOL
    "Use SDL_image's pinned stb/ImageIO PNG backend without an extra zlib ABI")
add_subdirectory(vendored/SDL_image EXCLUDE_FROM_ALL)

# Executable definition
set(REALMZ_EXECUTABLE_SRC src/realmz_orig/main.c)
set(SOURCES
    src/realmz_orig/Display.c
    src/realmz_orig/GWorldInit.c
    src/realmz_orig/GetVersStr.c
    src/realmz_orig/MacrocosmMain.c
    src/realmz_orig/MyrRealmz.c
    src/realmz_orig/actionpicker.c
    src/realmz_orig/age.c
    src/realmz_orig/attack.c
    src/realmz_orig/beast.c
    src/realmz_orig/bodycount.c
    src/realmz_orig/bodyfield.c
    src/realmz_orig/bodyground.c
    src/realmz_orig/booty.c
    src/realmz_orig/buildmonster.c
    src/realmz_orig/buttonchoice.c
    src/realmz_orig/cansee.c
    src/realmz_orig/cast.c
    src/realmz_orig/castspell.c
    src/realmz_orig/centerfield.c
    src/realmz_orig/centerpict.c
    src/realmz_orig/centerstage.c
    src/realmz_orig/change.c
    src/realmz_orig/checkforenemy.c
    src/realmz_orig/checkforsecret.c
    src/realmz_orig/checkfortype.c
    src/realmz_orig/checkkeypad.c
    src/realmz_orig/checkmoneypool.c
    src/realmz_orig/checkrange-checkforitem.c
    src/realmz_orig/choice.c
    src/realmz_orig/class.c
    src/realmz_orig/collide.c
    src/realmz_orig/combat.c
    src/realmz_orig/combaterrors.c
    src/realmz_orig/combatinfo-combatchoice.c
    src/realmz_orig/combatmap.c
    src/realmz_orig/combatsetup.c
    src/realmz_orig/combatupdate-2.c
    src/realmz_orig/compactitems.c
    src/realmz_orig/contactinfo.c
    src/realmz_orig/convert.c
    src/realmz_orig/delay.c
    src/realmz_orig/destroymonster.c
    src/realmz_orig/drawbody.c
    src/realmz_orig/dropitem.c
    src/realmz_orig/editspellnames.c
    src/realmz_orig/editstring.c
    src/realmz_orig/emptyque.c
    src/realmz_orig/encounters.c
    src/realmz_orig/fastplot.c
    src/realmz_orig/fileprep.c
    src/realmz_orig/files.c
    src/realmz_orig/flashmessage.c
    src/realmz_orig/flashrange-loaddoor.c
    src/realmz_orig/getbattleorder.c
    src/realmz_orig/getchoice.c
    src/realmz_orig/getnumspells.c
    src/realmz_orig/getrange.c
    src/realmz_orig/getscroll.c
    src/realmz_orig/getselection.c
    src/realmz_orig/getup.c
    src/realmz_orig/getword-gender.c
    src/realmz_orig/handlemenuchoice.c
    src/realmz_orig/heal.c
    src/realmz_orig/iconpict.c
    src/realmz_orig/inwindow.c
    src/realmz_orig/items.c
    src/realmz_orig/killbody.c
    src/realmz_orig/levelup.c
    src/realmz_orig/loaditem.c
    src/realmz_orig/loadland-loadpixmap.c
    src/realmz_orig/loadsavedgame.c
    src/realmz_orig/loadspell.c
    src/realmz_orig/lookupicon.c
    src/realmz_orig/makescroll.c
    src/realmz_orig/mapstuff.c
    src/realmz_orig/menuinit.c
    src/realmz_orig/misc.c
    src/realmz_orig/modify-cancast.c
    src/realmz_orig/moveicon.c
    src/realmz_orig/movemonster.c
    src/realmz_orig/moveparty.c
    src/realmz_orig/movie.c
    src/realmz_orig/music.c
    src/realmz_orig/name.c
    src/realmz_orig/newcharacter.c
    src/realmz_orig/newland.c
    src/realmz_orig/number.c
    src/realmz_orig/orderform.c
    src/realmz_orig/partyloss.c
    src/realmz_orig/partyselect.c
    src/realmz_orig/partyselect-helpers.cpp
    src/realmz_orig/placemonster.c
    src/realmz_orig/ploticon.c
    src/realmz_orig/plus-minus.c
    src/realmz_orig/pool.c
    src/realmz_orig/portrait-picklock.c
    src/realmz_orig/pref.c
    src/realmz_orig/question.c
    src/realmz_orig/race.c
    src/realmz_orig/reduce.c
    src/realmz_orig/removeitem.c
    src/realmz_orig/resist.c
    src/realmz_orig/resolvespell.c
    src/realmz_orig/save-direction-order.c
    src/realmz_orig/saveshop.c
    src/realmz_orig/setfree.c
    src/realmz_orig/setupnewgame.c
    src/realmz_orig/share-movecost-dialog.c
    src/realmz_orig/shift.c
    src/realmz_orig/showabout.c
    src/realmz_orig/showcharge.c
    src/realmz_orig/showcondition.c
    src/realmz_orig/showcondition2.c
    src/realmz_orig/showdamage-loaddark-cansee.c
    src/realmz_orig/showitems-showspecial.c
    src/realmz_orig/showlogo.c
    src/realmz_orig/showque.c
    src/realmz_orig/showrange.c
    src/realmz_orig/showresults.c
    src/realmz_orig/showspellinfo.c
    src/realmz_orig/showspelllist.c
    src/realmz_orig/showstats.c
    src/realmz_orig/showtag.c
    src/realmz_orig/showtargets.c
    src/realmz_orig/spelleffect.c
    src/realmz_orig/spelllist.c
    src/realmz_orig/spellselect.c
    src/realmz_orig/spelltargets.c
    src/realmz_orig/stack.c
    src/realmz_orig/swap.c
    src/realmz_orig/takegold.c
    src/realmz_orig/temple.c
    src/realmz_orig/textbox-time.c
    src/realmz_orig/threed.c
    src/realmz_orig/tickcheck.c-updatetorch.c
    src/realmz_orig/tomissle-weap.c
    src/realmz_orig/updatearrow.c
    src/realmz_orig/updatebooty.c
    src/realmz_orig/updatecanlist.c
    src/realmz_orig/updatechar.c
    src/realmz_orig/updatecharinfo.c
    src/realmz_orig/updatecharshort.c
    src/realmz_orig/updatecontrols.c
    src/realmz_orig/updatefat.c
    src/realmz_orig/updateitems.c
    src/realmz_orig/updatelight.c
    src/realmz_orig/updatemoney.c
    src/realmz_orig/updatepictbox.c
    src/realmz_orig/updatespec.c
    src/realmz_orig/updatespell.c
    src/realmz_orig/viewcharacter.c
    src/realmz_orig/warn.c
    src/realmz_orig/wear.c
    src/RealmzCocoa.c
    src/EventManager.cpp
    src/FileManager.cpp
    src/Font.cpp
    src/MemoryManager.cpp
    src/MenuManager.cpp
    src/PortMenuLogic.cpp
    src/PortPrefs.cpp
    src/presentation/LegacyPresentationContext.c
    src/presentation/LegacyPartySelection.c
    src/presentation/LegacyGameSnapshotSource.cpp
    src/presentation/SemanticInputBoundary.cpp
    src/QuickDraw.cpp
    src/ResourceManager.cpp
    src/SDLHelpers.cpp
    src/SoundManager.cpp
    src/UserDataImport.cpp
    src/UserDataPaths.cpp
    src/WindowManager.cpp
)

set(realmz_lib_LINK_LIBRARIES
    SDL3::SDL3-shared
    SDL3_ttf::SDL3_ttf
    SDL3_image::SDL3_image
    z
    phosg::phosg
    resource_file::resource_file
)

if(APPLE)
    list(APPEND SOURCES src/macos/MenuController.mm src/macos/WindowAspect.mm)
    set_source_files_properties(src/macos/MenuController.mm src/macos/WindowAspect.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    list(APPEND realmz_lib_LINK_LIBRARIES
        "-framework Foundation"
        "-framework Cocoa"
    )
elseif(WIN32)
    list(APPEND SOURCES src/windows/MenuController.cpp src/windows/WinMenuController.cpp)
    list(APPEND realmz_lib_LINK_LIBRARIES
        bcrypt # Used by phosg::phosg during compilation for windows targets
    )
endif()

# Installed in Contents/Resources
set(RESOURCE_FILES
    resources/realmz.rsrc
    bundle/AppIcon.icns
    "resources/Black Chancery.ttf"
    "resources/Black Chancery Bold.ttf"
    "resources/ChicagoFLF.ttf"
    "vendored/Inter/InterVariable.ttf"
)
set_source_files_properties(${RESOURCE_FILES} PROPERTIES
    MACOSX_PACKAGE_LOCATION Resources
)

# Installed in Contents/Resources/Data Files
set(DATA_RESOURCE_FILES
    "base/Realmz/Data Files/The Family Jewels.rsrc"
    "base/Realmz/Data Files/Data ID"
    "base/Realmz/Data Files/Data ID.rsrc"
    "base/Realmz/Data Files/Custom Names.rsrc"
    "base/Realmz/Data Files/Scenario Names.rsrc"
    "base/Realmz/Data Files/Portraits.rsrc"
    "base/Realmz/Data Files/Tacticals.rsrc"
    "base/Realmz/Data Files/Combat Data BD"
    "base/Realmz/Data Files/Data AD"
    "base/Realmz/Data Files/Data Caste"
    "base/Realmz/Data Files/Data Caste.rsrc"
    "base/Realmz/Data Files/Data Castle BD"
    "base/Realmz/Data Files/Data Desert BD"
    "base/Realmz/Data Files/Data ID"
    "base/Realmz/Data Files/Data P BD"
    "base/Realmz/Data Files/Data Race"
    "base/Realmz/Data Files/Data Race.rsrc"
    "base/Realmz/Data Files/Data S"
    "base/Realmz/Data Files/Data S.rsrc"
    "base/Realmz/Data Files/Data SUB BD"
    "base/Realmz/Data Files/Data Snow BD"
    "base/Realmz/Data Files/Data Swamp BD"
)
set_source_files_properties(${DATA_RESOURCE_FILES} PROPERTIES
    MACOSX_PACKAGE_LOCATION "Resources/Data\ Files"
)

# All the sources besides the executable source files with main functions should
# be shared as if they were a library to enable test executables
add_library(realmz_lib STATIC ${SOURCES})
set_target_properties(realmz_lib PROPERTIES C_STANDARD 90)
target_compile_definitions(realmz_lib PUBLIC REALMZ_COCOA=1)
if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR
        CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    message("-- REALMZ_DEBUG is enabled because CMAKE_BUILD_TYPE is \"${CMAKE_BUILD_TYPE}\"")
    target_compile_definitions(realmz_lib PUBLIC REALMZ_DEBUG=1)
else()
    message("-- REALMZ_DEBUG is disabled because CMAKE_BUILD_TYPE is \"${CMAKE_BUILD_TYPE}\"")
endif()
target_compile_options(realmz_lib PUBLIC -fpascal-strings)
target_include_directories(realmz_lib PUBLIC ${SDL3_INCLUDE_DIRS} src src/realmz_orig)
if(APPLE)
    # WindowAspect.mm intentionally touches the private SDL_Window aspect fields
    # in this pinned, in-tree SDL version; see that source for the AppKit crash
    # avoided by the shim. Remove these includes with the shim when SDL carries
    # an upstream-safe aspect-clear implementation.
    target_include_directories(realmz_lib PRIVATE
        "${PROJECT_SOURCE_DIR}/vendored/SDL/src"
        "${PROJECT_BINARY_DIR}/vendored/SDL/include-config-$<LOWER_CASE:$<CONFIG>>/build_config")
endif()

if(WIN32)
  target_compile_definitions(realmz_lib PUBLIC PHOSG_WINDOWS=1)
endif()

target_link_libraries(realmz_lib
    realmz_presentation_core
    realmz_asset_core
    realmz_userdata_core
    ${realmz_lib_LINK_LIBRARIES}
)

add_executable(Realmz
    ${REALMZ_EXECUTABLE_SRC}
    ${RESOURCE_FILES}
    ${DATA_RESOURCE_FILES}
)
set_target_properties(Realmz PROPERTIES C_STANDARD 90)

target_compile_definitions(Realmz PUBLIC REALMZ_COCOA=1)
target_compile_options(Realmz PUBLIC -fpascal-strings)
target_include_directories(Realmz PUBLIC ${SDL3_INCLUDE_DIRS} src src/realmz_orig src/macos)
target_link_libraries(Realmz
    PUBLIC
    realmz_lib
)

if(NOT WIN32 AND REALMZ_ENABLE_APP_SANITIZERS AND
        NOT CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(realmz_lib PRIVATE
        -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_compile_options(Realmz PRIVATE
        -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(Realmz PRIVATE -fsanitize=address,undefined)
endif()

# A packaged app receives the same phase-one files through install() below.
# Copying into the raw build tree is opt-in because cloud-backed workspaces can
# make this developer convenience dramatically slower than compilation.
if(REALMZ_COPY_DEVELOPMENT_RESOURCES)
  add_custom_command(TARGET Realmz POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:Realmz>/Data Files"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/base/Realmz/Data Files" "$<TARGET_FILE_DIR:Realmz>/Data Files"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:Realmz>/Scenarios/Tutorial"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/base/Realmz/Scenarios/Tutorial" "$<TARGET_FILE_DIR:Realmz>/Scenarios/Tutorial"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:Realmz>/Scenarios/City of Bywater"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/base/Realmz/Scenarios/City of Bywater" "$<TARGET_FILE_DIR:Realmz>/Scenarios/City of Bywater"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:Realmz>/Character Files"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/base/Realmz/Character Files" "$<TARGET_FILE_DIR:Realmz>/Character Files"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/resources/realmz.rsrc" "$<TARGET_FILE_DIR:Realmz>/realmz.rsrc"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/resources/Black Chancery.ttf" "$<TARGET_FILE_DIR:Realmz>/Black Chancery.ttf"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/resources/Black Chancery Bold.ttf" "$<TARGET_FILE_DIR:Realmz>/Black Chancery Bold.ttf"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/resources/ChicagoFLF.ttf" "$<TARGET_FILE_DIR:Realmz>/ChicagoFLF.ttf"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:Realmz>/Remastered"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/assets/remastered/scopes/phase1.placeholder-manifest.json" "$<TARGET_FILE_DIR:Realmz>/Remastered/phase1.placeholder-manifest.json"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/assets/remastered/scopes/phase1.runtime-manifest.json" "$<TARGET_FILE_DIR:Realmz>/Remastered/phase1.runtime-manifest.json"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/assets/remastered/scopes/phase1.census.json" "$<TARGET_FILE_DIR:Realmz>/Remastered/phase1.census.json"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/assets/remastered/scopes/phase1.json" "$<TARGET_FILE_DIR:Realmz>/Remastered/phase1.scope.json"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:Realmz>/Remastered/style-proof/generation/outputs"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets/remastered/style-proof/generation/outputs" "$<TARGET_FILE_DIR:Realmz>/Remastered/style-proof/generation/outputs"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:Realmz>/Notices"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/bundle/Notices" "$<TARGET_FILE_DIR:Realmz>/Notices"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE" "$<TARGET_FILE_DIR:Realmz>/Notices/LICENSE"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/docs/CONTENT_PROVENANCE.md" "$<TARGET_FILE_DIR:Realmz>/Notices/CONTENT_PROVENANCE.md"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/vendored/Inter/InterVariable.ttf" "$<TARGET_FILE_DIR:Realmz>"
  )
endif()

set(TEST_EXECUTABLES "GraphicsTest")
foreach(TEST_EXECUTABLE ${TEST_EXECUTABLES})
    # This legacy visual sample ends in an intentional busy loop and does not
    # provide automated assertions. Keep it available as an explicit target,
    # but do not let an `all`/preinstall build pull it into release packaging.
    add_executable(${TEST_EXECUTABLE} MACOSX_BUNDLE EXCLUDE_FROM_ALL
        src/tests/${TEST_EXECUTABLE}.cpp
        ${RESOURCE_FILES}
        ${DATA_RESOURCE_FILES}
    )
    target_compile_definitions(${TEST_EXECUTABLE} PUBLIC REALMZ_COCOA=1)
    target_compile_options(${TEST_EXECUTABLE} PUBLIC -fpascal-strings)
    target_include_directories(${TEST_EXECUTABLE} PUBLIC ${SDL3_INCLUDE_DIRS} src src/realmz_orig src/macos)
    target_link_libraries(${TEST_EXECUTABLE}
        PUBLIC
        realmz_lib
    )

    if(NOT WIN32 AND REALMZ_ENABLE_APP_SANITIZERS AND
            NOT CMAKE_BUILD_TYPE STREQUAL "Release")
        target_compile_options(${TEST_EXECUTABLE} PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(${TEST_EXECUTABLE} PRIVATE
            -fsanitize=address,undefined)
    endif()
endforeach()

# Scenario data installed in Contents/Resources/Scenarios
set(SCENARIO_NAMES
    "Tutorial"
    "City of Bywater"
)
foreach(SCENARIO_NAME ${SCENARIO_NAMES})
file(GLOB SCENARIO_RESOURCE_FILES "base/Realmz/Scenarios/${SCENARIO_NAME}/*")
target_sources(Realmz PRIVATE ${SCENARIO_RESOURCE_FILES})
endforeach()

# Character data installed in Contents/Resources/Character Files
file(GLOB CHARACTER_DATA_FILES "base/Realmz/Character Files/*")
target_sources(Realmz PRIVATE ${CHARACTER_DATA_FILES})

if(APPLE)
    # Install data files to <bundle_root>/Resources
    install(FILES ${RESOURCE_FILES} DESTINATION ".")
    install(FILES ${DATA_RESOURCE_FILES} DESTINATION "Data Files")
    install(DIRECTORY
        "${CMAKE_SOURCE_DIR}/base/Realmz/Scenarios/Tutorial"
        "${CMAKE_SOURCE_DIR}/base/Realmz/Scenarios/City of Bywater"
        DESTINATION "Scenarios")
    install(FILES ${CHARACTER_DATA_FILES} DESTINATION "Character Files")
    install(FILES
        "assets/remastered/scopes/phase1.placeholder-manifest.json"
        DESTINATION "Remastered"
        RENAME "phase1.placeholder-manifest.json")
    install(FILES
        "assets/remastered/scopes/phase1.runtime-manifest.json"
        DESTINATION "Remastered"
        RENAME "phase1.runtime-manifest.json")
    install(FILES
        "assets/remastered/scopes/phase1.census.json"
        DESTINATION "Remastered"
        RENAME "phase1.census.json")
    install(FILES
        "assets/remastered/scopes/phase1.json"
        DESTINATION "Remastered"
        RENAME "phase1.scope.json")
    install(DIRECTORY
        "assets/remastered/style-proof/generation/outputs/"
        DESTINATION "Remastered/style-proof/generation/outputs"
        FILES_MATCHING PATTERN "*.png")
    install(FILES
        LICENSE
        bundle/Notices/ATTRIBUTION.md
        bundle/Notices/MODIFICATIONS.md
        docs/CONTENT_PROVENANCE.md
        DESTINATION "Notices")

    # These are the only non-system dynamic libraries in the Release graph.
    # Install the known pinned targets explicitly: generic runtime-dependency
    # scanning cannot resolve their @rpath names reliably while CPack is
    # constructing the bundle staging tree.
    install(TARGETS
        SDL3-shared
        SDL3_ttf-shared
        SDL3_image-shared
        LIBRARY DESTINATION "lib")
    set_target_properties(
        SDL3-shared
        SDL3_ttf-shared
        SDL3_image-shared
        PROPERTIES INSTALL_RPATH "@loader_path")

    # Manually set the rpath to avoid CMake automatically adding hardcoded build machine paths
    set_target_properties(Realmz PROPERTIES
        INSTALL_RPATH "@executable_path/../Resources/lib"
        BUILD_WITH_INSTALL_RPATH TRUE
    )
elseif(WIN32)
    install(TARGETS Realmz DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES ${RESOURCE_FILES} DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES ${DATA_RESOURCE_FILES} DESTINATION "${CMAKE_INSTALL_BINDIR}/Data Files")
    install(DIRECTORY
        "base/Realmz/Scenarios/Tutorial"
        "base/Realmz/Scenarios/City of Bywater"
        DESTINATION "${CMAKE_INSTALL_BINDIR}/Scenarios")
    install(FILES ${CHARACTER_DATA_FILES} DESTINATION "${CMAKE_INSTALL_BINDIR}/Character Files")

    # CMake's RUNTIME_DEPENDENCIES argument to install should work to include necessary
    # runtime dlls alongside the Realmz executable, but unfortunately (as of 4.1.1) it does
    # not support the cross-compiling use case. Instead, we manually include these.
    install(IMPORTED_RUNTIME_ARTIFACTS
        SDL3-shared
        SDL3_ttf-shared
        SDL3_image-shared)

    # The LLVM MinGW toolchain injects a couple of runtime dependencies that we have to install
    # alongside the executable.
    if(CMAKE_CROSSCOMPILING AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        find_file(libc++ NAMES libc++.dll PATH_SUFFIXES bin REQUIRED)
        message("Found c++ lib at ${libc++}")
        find_file(libunwind NAMES libunwind.dll PATH_SUFFIXES bin REQUIRED)
        message("Found unwind lib at ${libunwind}")
        find_file(libasan NAMES libclang_rt.asan_dynamic-x86_64.dll PATH_SUFFIXES bin REQUIRED)
        message("Found asan lib at ${libasan}")
        install(FILES ${libc++} ${libunwind} ${libasan} DESTINATION ${CMAKE_INSTALL_BINDIR})
    endif()
endif()


# CPack Package Installer Section
include(InstallRequiredSystemLibraries)
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGE_VERSION_MAJOR "${Realmz_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${Realmz_VERSION_MINOR}")
set(CPACK_SOURCE_GENERATOR "TGZ")

if(WIN32)
    set(CPACK_GENERATOR "NSIS;ZIP")
    set(CPACK_NSIS_PACKAGE_NAME "${CMAKE_PROJECT_NAME}")
elseif(APPLE)
    configure_file(bundle/Info.plist.in Info.plist)
    set(CPACK_GENERATOR "Bundle")
    set(CPACK_PACKAGE_NAME "${REALMZ_PRODUCT_NAME}")
    set(CPACK_BUNDLE_NAME "${REALMZ_PRODUCT_NAME}")
    set(CPACK_BUNDLE_PLIST "${PROJECT_BINARY_DIR}/Info.plist")
    set(CPACK_BUNDLE_ICON "${PROJECT_SOURCE_DIR}/bundle/AppIcon.icns")
    set(CPACK_BUNDLE_STARTUP_COMMAND "${CMAKE_CURRENT_BINARY_DIR}/Realmz")
endif()
include(CPack)
