# Changelog

## [2.2.0-rc.1](https://github.com/NotRexed/Hookshot/compare/v2.1.0...v2.2.0-rc.1) (2026-11-11)


### Features

* an Upscaler choice Off: no upscaling at all, the game's own network left out too ([#2097](https://github.com/NotRexed/Hookshot/issues/2097)) ([215ab3d](https://github.com/NotRexed/Hookshot/commit/215ab3dd6536cb21fc67c2e89c31c10a18df555c))
* make_game.py --fetch-fsr-runtime fetches AMD's FSR 4 runtime ([#1866](https://github.com/NotRexed/Hookshot/issues/1866)) ([d7dd931](https://github.com/NotRexed/Hookshot/commit/d7dd931d2762217ef67b4ba4b0aa10043a82056d))
* **Menu:** a row that turns the preset to Custom keeps the preset's memory budgets ([#1358](https://github.com/NotRexed/Hookshot/issues/1358)) ([26b2f74](https://github.com/NotRexed/Hookshot/commit/26b2f7445d2351fc054d9fe7621d7e8790875865))
* **menu:** a V-Sync row (Auto, On, Off) ([#2041](https://github.com/NotRexed/Hookshot/issues/2041)) ([b0361d2](https://github.com/NotRexed/Hookshot/commit/b0361d2407ad8de9f1637933fe9e1405d82dbbc8))
* **update:** an update track (Release, Release candidate, Nightly) in the builders and the game ([#2061](https://github.com/NotRexed/Hookshot/issues/2061)) ([6cecdd1](https://github.com/NotRexed/Hookshot/commit/6cecdd18b5301984feb99535cac323f8d1e90b6a))
* **video:** support 48:9 triple-monitor picture geometry ([#2052](https://github.com/NotRexed/Hookshot/issues/2052)) ([b46b60f](https://github.com/NotRexed/Hookshot/commit/b46b60f036ea739ca8edbde851b0f295a5ac7977))


### Bug Fixes

* **builder:** a drive that cannot read its files is said as that, with its repair ([#2063](https://github.com/NotRexed/Hookshot/issues/2063)) ([#2072](https://github.com/NotRexed/Hookshot/issues/2072)) ([5c59382](https://github.com/NotRexed/Hookshot/commit/5c59382e5d290487394cd6c7c7e480c009b2b104))
* **builder:** a package with a Python of its own runs no other ([#2045](https://github.com/NotRexed/Hookshot/issues/2045)) ([#2088](https://github.com/NotRexed/Hookshot/issues/2088)) ([d1d3888](https://github.com/NotRexed/Hookshot/commit/d1d3888c2444ca13e96001ec14388715831b24c1))
* **builder:** retain bounded Linux Play startup diagnostics ([#2024](https://github.com/NotRexed/Hookshot/issues/2024)) ([44e56b0](https://github.com/NotRexed/Hookshot/commit/44e56b0c2b7d2bf235f25455666841595ef2fc85))
* **builder:** the Python setup checks the setup list as setup.c does ([#2057](https://github.com/NotRexed/Hookshot/issues/2057)) ([812f28d](https://github.com/NotRexed/Hookshot/commit/812f28d5389480746564af3e58e1b12e99f29534))
* **build:** run offline bake with portable zstd and Clang 18 ([#1954](https://github.com/NotRexed/Hookshot/issues/1954)) ([51e1d98](https://github.com/NotRexed/Hookshot/commit/51e1d98f1b0f9f64ca439ea0708f870659f13c3e))
* **debug:** the flight lets Link go for events and respawns ([#2050](https://github.com/NotRexed/Hookshot/issues/2050)) ([c2a8c92](https://github.com/NotRexed/Hookshot/commit/c2a8c922e104b7f7007961416494b34e5a5c67a6))
* **input:** turn a frozen gyroscope's sensors off and on again (gyro dead after Alt+Tab under Steam) ([#2036](https://github.com/NotRexed/Hookshot/issues/2036)) ([b76f12d](https://github.com/NotRexed/Hookshot/commit/b76f12d12ad6c7109f73abcd9a201f3356c64a5a))
* **linux:** the updater follows a redirect only to another https link ([#2045](https://github.com/NotRexed/Hookshot/issues/2045)) ([#2086](https://github.com/NotRexed/Hookshot/issues/2086)) ([465ab86](https://github.com/NotRexed/Hookshot/commit/465ab86dbe44825f33e085d3397bb3585f89ca75))
* **menu:** keep the mouse free after Escape then F1 ([#2035](https://github.com/NotRexed/Hookshot/issues/2035)) ([1a43a1e](https://github.com/NotRexed/Hookshot/commit/1a43a1e2942c7793a19115d2bd0f509adfac0195))
* **menu:** the Ultrawide row says Off (16:9) when it is off ([#2078](https://github.com/NotRexed/Hookshot/issues/2078)) ([bb94350](https://github.com/NotRexed/Hookshot/commit/bb94350a2098b10947fa865eadc0918b3d9280d3))
* **nup:** exclude digest-mismatched stand-in sources ([#2019](https://github.com/NotRexed/Hookshot/issues/2019)) ([2009c5f](https://github.com/NotRexed/Hookshot/commit/2009c5f98ce32631893bbf4f36c788aa91b8c9fb))
* **prompts:** the controls page's leader lines end on their buttons ([#2089](https://github.com/NotRexed/Hookshot/issues/2089)) ([798f325](https://github.com/NotRexed/Hookshot/commit/798f32587e3fb7728a7f26322587bc30c5650c29))
* **trace:** Separate present-call and virtual-core waits ([#1983](https://github.com/NotRexed/Hookshot/issues/1983)) ([fc7a982](https://github.com/NotRexed/Hookshot/commit/fc7a982813df880d941eb808a24c10cff3463ebf))
* **vi:** prevent frame-time jitter from speeding up the game clock ([#1911](https://github.com/NotRexed/Hookshot/issues/1911)) ([f4f2c74](https://github.com/NotRexed/Hookshot/commit/f4f2c743fec4e84beef42ffad4fbf80c77323f70))
* **vi:** the pick-up counter fades again above a 60 fps cap ([#1832](https://github.com/NotRexed/Hookshot/issues/1832)) ([#2006](https://github.com/NotRexed/Hookshot/issues/2006)) ([f915af9](https://github.com/NotRexed/Hookshot/commit/f915af93ed0061cf4a7514c66d34ab9de1f95bf7))
* **vk:** refresh pipeline states during Builder preparation ([#1980](https://github.com/NotRexed/Hookshot/issues/1980)) ([ca082bf](https://github.com/NotRexed/Hookshot/commit/ca082bfc2ad870e9250aa8e6d5f4648e46c2b083))
* **vk:** task + mesh draws through the compute fallback on Navi 21 too ([#996](https://github.com/NotRexed/Hookshot/issues/996)) ([#1992](https://github.com/NotRexed/Hookshot/issues/1992)) ([1396428](https://github.com/NotRexed/Hookshot/commit/1396428d0bfcde68b5c7bd63f9837564d9af1a05))
* **vk:** tree-shadow flicker without the TAAU network: the shadow mask evened out over frames (rebase of [#477](https://github.com/NotRexed/Hookshot/issues/477)) ([#1581](https://github.com/NotRexed/Hookshot/issues/1581)) ([746fa11](https://github.com/NotRexed/Hookshot/commit/746fa11582bd05518e20b2ce890e97d9f8d8bb22))


### Build System

* **linux:** dav1d and the Vulkan back end's headers are pinned by what they unpack to ([#2045](https://github.com/NotRexed/Hookshot/issues/2045)) ([#2087](https://github.com/NotRexed/Hookshot/issues/2087)) ([9a37fac](https://github.com/NotRexed/Hookshot/commit/9a37facb8234a922da6a95825ea151e7ca633104))
* **release:** a contributed pipeline list from [#2034](https://github.com/NotRexed/Hookshot/issues/2034), RTX 4060 laptop with frame generation and HDR, 6597 pipelines ([#2082](https://github.com/NotRexed/Hookshot/issues/2082)) ([51d468f](https://github.com/NotRexed/Hookshot/commit/51d468f91b2538b3afb2dd6c9f9e6124d23b03aa))
* **release:** the pipeline tour's list, 11227 pipelines (6238 no other list holds) ([#2065](https://github.com/NotRexed/Hookshot/issues/2065)) ([a74f6a8](https://github.com/NotRexed/Hookshot/commit/a74f6a8846c6a1dc063895a6730069112ed3c737))
* **release:** two contributed pipeline lists from [#2034](https://github.com/NotRexed/Hookshot/issues/2034) (RTX 5090 ultrawide DLAA, RX 9070 XT) ([#2075](https://github.com/NotRexed/Hookshot/issues/2075)) ([21fa136](https://github.com/NotRexed/Hookshot/commit/21fa136590ab862f761852effc34395982072c90))


### Continuous Integration

* **appimage:** the object cache keeps the objects a build used ([#2090](https://github.com/NotRexed/Hookshot/issues/2090)) ([eb689bc](https://github.com/NotRexed/Hookshot/commit/eb689bcb81461d2d3d09a591af20a78deb2b319d))
* cache the AppImage's back-end objects for pull requests, and a lighter deps cache ([#2079](https://github.com/NotRexed/Hookshot/issues/2079)) ([37031ed](https://github.com/NotRexed/Hookshot/commit/37031ed7e8eaaa1bed03f707d1b1f31d2a5c0a62))
* unpack LLVM beside the other dependencies, not before them ([#2091](https://github.com/NotRexed/Hookshot/issues/2091)) ([ceb8745](https://github.com/NotRexed/Hookshot/commit/ceb8745e05d8ee83b401c04ceb20a02feb41792d))


### Documentation

* **readme:** NVIDIA DLSS on Linux, one entry for each way to build ([#2056](https://github.com/NotRexed/Hookshot/issues/2056)) ([#2074](https://github.com/NotRexed/Hookshot/issues/2074)) ([23840a4](https://github.com/NotRexed/Hookshot/commit/23840a481bf72786d8cee8c6d72be1944511f1f1))
* tested configurations: RTX 5090, 621.14, Windows 11 (v2.0.14) ([#2037](https://github.com/NotRexed/Hookshot/issues/2037)) ([28a60b5](https://github.com/NotRexed/Hookshot/commit/28a60b5e38866775d80f16f4d796a1c47bbed279))


### Tests

* fix main's three red checks (fixture fopen deprecation; DLSS toggle stub lacks switching; actionlint SC2012) ([#2092](https://github.com/NotRexed/Hookshot/issues/2092)) ([e3b1964](https://github.com/NotRexed/Hookshot/commit/e3b1964ed8e8e71faaed256dc21d6c55105eb7c0))
* main's red checks after [#1866](https://github.com/NotRexed/Hookshot/issues/1866) and [#2041](https://github.com/NotRexed/Hookshot/issues/2041) (finish-block 'a'; vsync fixture getenv deprecation) ([#2109](https://github.com/NotRexed/Hookshot/issues/2109)) ([7d24a73](https://github.com/NotRexed/Hookshot/commit/7d24a736dae8a2430988d459aba287a303877bdb))
* main's source checks pass again after [#2061](https://github.com/NotRexed/Hookshot/issues/2061) and [#2046](https://github.com/NotRexed/Hookshot/issues/2046) ([#2068](https://github.com/NotRexed/Hookshot/issues/2068)) ([96ad468](https://github.com/NotRexed/Hookshot/commit/96ad46809fea43f1c04c6de69ae19abf1813fabc))


### Miscellaneous Chores

* **main:** release 2.1.0-rc.3 ([#2062](https://github.com/NotRexed/Hookshot/issues/2062)) ([9d5a292](https://github.com/NotRexed/Hookshot/commit/9d5a2927c4c7c2624bcd8cf01dfa70c47c6037f5))
* **release:** record 2.1.0 as released ([309f86b](https://github.com/NotRexed/Hookshot/commit/309f86b1cd88068106c40c02892854712f9db0b1))

## [2.1.0](https://github.com/NotRexed/Hookshot/releases/tag/v2.1.0) (2026-11-11)

Release candidate [v2.1.0-rc.2](https://github.com/NotRexed/Hookshot/releases/tag/v2.1.0-rc.2), published as 2.1.0. Its changes are in its release candidates' entries below.

## [2.1.0-rc.3](https://github.com/NotRexed/Hookshot/compare/v2.1.0-rc.2...v2.1.0-rc.3) (2026-11-11)


### Features

* **update:** an update track (Release, Release candidate, Nightly) in the builders and the game ([#2061](https://github.com/NotRexed/Hookshot/issues/2061)) ([6cecdd1](https://github.com/NotRexed/Hookshot/commit/6cecdd18b5301984feb99535cac323f8d1e90b6a))

## [2.1.0-rc.2](https://github.com/NotRexed/Hookshot/compare/v2.1.0-rc.1...v2.1.0-rc.2) (2026-11-11)


### Features

* **vk:** GPU- and settings-independent pipeline state list (HKSTATE2) ([#2046](https://github.com/NotRexed/Hookshot/issues/2046)) ([667ba79](https://github.com/NotRexed/Hookshot/commit/667ba79bd800c5040861a020d123bfd4a328f9f4))


### Bug Fixes

* **vk:** handle RDNA 1 wave32 and wave64 shader stages ([#1657](https://github.com/NotRexed/Hookshot/issues/1657)) ([fc8c858](https://github.com/NotRexed/Hookshot/commit/fc8c8586337c680d48917cbe3bd5ddbdef76f8a4))


### Build System

* **release:** a contributed pipeline list from an AMD card with FSR 4, 1273 pipelines ([#2044](https://github.com/NotRexed/Hookshot/issues/2044)) ([f8efe05](https://github.com/NotRexed/Hookshot/commit/f8efe05775be7a4db6450f486e199cdbd3a6e53a))


### Continuous Integration

* **release:** list every commit type in the changelog ([#2055](https://github.com/NotRexed/Hookshot/issues/2055)) ([737e4f5](https://github.com/NotRexed/Hookshot/commit/737e4f52ded3d8082f3db6cc9d0e535848c63158))


### Tests

* **display:** the startup fixture stubs driver_cache_room (main is red since [#2033](https://github.com/NotRexed/Hookshot/issues/2033)) ([#2051](https://github.com/NotRexed/Hookshot/issues/2051)) ([d47206e](https://github.com/NotRexed/Hookshot/commit/d47206ed720d9f34cd4a04ea6caf148b1e236ba8))

## [2.1.0-rc.1](https://github.com/NotRexed/Hookshot/compare/v2.0.14...v2.1.0-rc.1) (2026-11-11)


### Features

* Add a control to disable camera centering ([2dcf210](https://github.com/NotRexed/Hookshot/commit/2dcf2100ad822c75a97d017b41a993a438036031))
* Add an option to disable automatic camera centering ([657da4f](https://github.com/NotRexed/Hookshot/commit/657da4f2baee8a90b2a4d686fb2b74e93cb60748))
* Audio tab in the Insert menu with Volume and Mute in background ([52ca067](https://github.com/NotRexed/Hookshot/commit/52ca067c7c08c7139f69b7bad16e9c0cd2d46f43))
* Audio tab in the Insert menu with Volume and Mute in background ([df2d84c](https://github.com/NotRexed/Hookshot/commit/df2d84ca4b7eba04d3ef62ea7de1979fbd616a8b))
* **builder:** let the player choose how much memory a build may take ([90055d0](https://github.com/NotRexed/Hookshot/commit/90055d06e895b60e53455728c3ffdedeaa9b9b96))
* **builder:** the Linux builder looks like the Windows one ([8c1b5e9](https://github.com/NotRexed/Hookshot/commit/8c1b5e9fff9d67a4106336f5bb8ffa1ba1031667))
* **builder:** the Linux builder looks like the Windows one ([8f2e1a1](https://github.com/NotRexed/Hookshot/commit/8f2e1a1f38710157575ff793b5ab4a207d0e7221))
* **builder:** use the saved build memory choice for updates ([86ff22c](https://github.com/NotRexed/Hookshot/commit/86ff22cd03ac10e0418ff4f239c83691249059a6))
* **builder:** warn before building on a hard drive ([b526a84](https://github.com/NotRexed/Hookshot/commit/b526a841f362becca6c0991c6c003583fe0b586b))
* **builder:** warn before building on a hard drive ([c769305](https://github.com/NotRexed/Hookshot/commit/c769305b3956a2b236ef7d05c707d97cdad97726))
* **build:** Probe for system Vulkan packages, add proper requirements ([3b84291](https://github.com/NotRexed/Hookshot/commit/3b84291bd270672feb4896f5a9269c003ce18711))
* **build:** Probe for system Vulkan packages, add proper requirements.txt ([155bfa2](https://github.com/NotRexed/Hookshot/commit/155bfa2c913c71684387ca4fa6c928cac5ac9f48))
* **linux:** opt-in NVIDIA DLSS in Linux Builder and AppImage ([484cfd1](https://github.com/NotRexed/Hookshot/commit/484cfd18199f8dacac9c88b45ff5cac99ed57baf))
* **linux:** opt-in NVIDIA DLSS in Linux Builder and AppImage ([66b86fb](https://github.com/NotRexed/Hookshot/commit/66b86fb5b25fdea977b547882be3806461948004))
* **menu:** a DualShock 4 or DualSense touchpad click opens the menu as F1 ([8790b8f](https://github.com/NotRexed/Hookshot/commit/8790b8f62aad4d01150f20dd3e2fef94185705e7))
* **menu:** a Mesh shaders row (Automatic, Native, Compute) ([#1971](https://github.com/NotRexed/Hookshot/issues/1971)) ([c8022e7](https://github.com/NotRexed/Hookshot/commit/c8022e77945b7932e73d1890eb3a8dd5329be972))
* **menu:** a Text size row for dialogue, Navi and signs ([4d0645f](https://github.com/NotRexed/Hookshot/commit/4d0645f96f09435a7ce48d94b3b76c339a8a562e))
* **menu:** a Text size row for dialogue, Navi and signs ([4bae30c](https://github.com/NotRexed/Hookshot/commit/4bae30c59809b3c2e670f842b0db0159fb6bda0c))
* **menu:** add 5120x2880 (5K) to Output resolution ([ce175ac](https://github.com/NotRexed/Hookshot/commit/ce175acf76c51cf25e8938d92d27f0c40247a861))
* **menu:** centre the menu in a 16:9 column on ultrawide ([70e2209](https://github.com/NotRexed/Hookshot/commit/70e220999bc5e108ca19b5158d4f645811674549))
* **menu:** DLAA as DLSS's Native render resolution, and Render resolution as a slider in real sizes ([#2000](https://github.com/NotRexed/Hookshot/issues/2000)) ([e187859](https://github.com/NotRexed/Hookshot/commit/e18785985454d086531d2e28f56f696619aad2f8))
* **menu:** Esc can open the Insert menu (Menu shortcut "Insert, F1 or Esc") ([#1998](https://github.com/NotRexed/Hookshot/issues/1998)) ([d926317](https://github.com/NotRexed/Hookshot/commit/d9263175f49c33b7ac17cc3635d44b2732dcfc8d))
* **menu:** let players choose the keyboard shortcut ([#1609](https://github.com/NotRexed/Hookshot/issues/1609)) ([191cfdc](https://github.com/NotRexed/Hookshot/commit/191cfdcfcb012d72f15d884a168a2d97c6a32396))
* **menu:** put the debug tools on the Debug tab ([e256616](https://github.com/NotRexed/Hookshot/commit/e256616591232fd7a0d57eea24bb4df641479cf7))
* **menu:** redesign the Debug tab, Inventory and Teleport pages ([c805d9b](https://github.com/NotRexed/Hookshot/commit/c805d9b99ed8aa86043e0d7e1ca5094be4bef79f))
* **menu:** save Output resolution as a size, and take any size from the config ([a78ee4d](https://github.com/NotRexed/Hookshot/commit/a78ee4d0e28958ddc301c84277200d4bc8a67bc7))
* **menu:** the debug tools' Teleport row opens the map list ([a4c3a8d](https://github.com/NotRexed/Hookshot/commit/a4c3a8d6f215b275cd50b40197dc6c14dec645b9))
* RecompLauncher support (recomplauncher.json, make_game --game/--prepare) ([ab5a1f6](https://github.com/NotRexed/Hookshot/commit/ab5a1f61d3354a42332e91f0c52804ad5da1daa3))
* **startup:** lower the start-up screen, smaller text, fade to black at the end ([8649805](https://github.com/NotRexed/Hookshot/commit/8649805c8e54b0f8889164c6a3497fe3b986c36f))
* **startup:** lower the start-up screen, smaller text, fade to black at the end ([fb60134](https://github.com/NotRexed/Hookshot/commit/fb601341b880e2d36c31f6bce0859b227f32a22b))
* **vk:** add AMD Anti-Lag 2 (VK_AMD_anti_lag) ([8ea4e7f](https://github.com/NotRexed/Hookshot/commit/8ea4e7fa5743ce56694a35fde3dc5a4ab27a254f))
* **vk:** add opt-in VRAM compaction and hold the driver's count below its limit ([3932421](https://github.com/NotRexed/Hookshot/commit/393242121aa6a07574d08e037b23c718abcc195c))
* **vk:** AMD Anti-Lag 2 through VK_AMD_anti_lag ([b9e7559](https://github.com/NotRexed/Hookshot/commit/b9e7559ed28aa5bd60e3476c5de8c91c2fd1301f))
* **vk:** Anisotropic filtering option, 16x by default, and the upscalers' texture LOD bias ([0af7629](https://github.com/NotRexed/Hookshot/commit/0af762907565077b8612a8afa20b0150030f298a))
* **vk:** Anisotropic filtering row, 16x by default ([d9b0ce2](https://github.com/NotRexed/Hookshot/commit/d9b0ce20965c018b3831e09264beaccbf7176ea8))
* **vk:** draw the menu, FPS counter and start-up screen with Dear ImGui ([509579b](https://github.com/NotRexed/Hookshot/commit/509579b05980e84a166c729bd357aaa3fa196e3e))
* **vk:** opt-in VRAM compaction (RX_VK_COMPACT=1): the textures' free space goes back to the driver ([d218438](https://github.com/NotRexed/Hookshot/commit/d21843848e12235c5438f959cbbb7b086a5a25ff))
* **vk:** trace effective presentation geometry on changes ([#1457](https://github.com/NotRexed/Hookshot/issues/1457)) ([a350759](https://github.com/NotRexed/Hookshot/commit/a350759032f66a4f3892aa9f47dcf8b060e9aa0e))
* **vk:** upscalers' texture LOD bias below the game's 3/4 render ([3b9ffcd](https://github.com/NotRexed/Hookshot/commit/3b9ffcda29d2e42328cbe3d7c78df802becbedd5))
* **vk:** warm the stage libraries from the program cache into the pipeline cache ([24ebac7](https://github.com/NotRexed/Hookshot/commit/24ebac70db0e9859d825da27aa3d1b91f04bd681))


### Bug Fixes

* a game folder whose deepest file passes 260 characters builds and plays ([#1468](https://github.com/NotRexed/Hookshot/issues/1468)) ([d231d17](https://github.com/NotRexed/Hookshot/commit/d231d17ba39133adde27a2893218eeceeca00478))
* a game folder whose deepest file passes 260 characters builds and plays ([#1468](https://github.com/NotRexed/Hookshot/issues/1468)) ([45c7418](https://github.com/NotRexed/Hookshot/commit/45c741874070b51720aa38525c62c992a5a0cccf))
* Address camera centering review feedback ([103e2cd](https://github.com/NotRexed/Hookshot/commit/103e2cd82cb55a0a9169d8dccd6a3b2a7ef7be47))
* **appimage:** copy sysroot library aliases by value for updates ([#1536](https://github.com/NotRexed/Hookshot/issues/1536)) ([ed66422](https://github.com/NotRexed/Hookshot/commit/ed6642256e5cd07e35bd5e013d9903ab37509432))
* **audio:** keep 512-frame power-of-two default on SDL2 fallback ([#1550](https://github.com/NotRexed/Hookshot/issues/1550)) ([fa85175](https://github.com/NotRexed/Hookshot/commit/fa85175b1adcca7086b70a6aba37316d2926b107))
* **build:** a game built from extracted folders refuses links in them ([233e072](https://github.com/NotRexed/Hookshot/commit/233e072ce8a9aa73804da0f8d4a7912cec822f97))
* **builder:** --cli says make_game's own error, on stdout too ([a3859f1](https://github.com/NotRexed/Hookshot/commit/a3859f1fee0873cdde31f2340d4bd0c7519ee1a8))
* **builder:** bound writes from compressed NUP members ([#1596](https://github.com/NotRexed/Hookshot/issues/1596)) ([56e31e7](https://github.com/NotRexed/Hookshot/commit/56e31e78688749c9fa57fcd8e8ebbaf08bf9c737))
* **builder:** constrain the Show details hit area ([d45e262](https://github.com/NotRexed/Hookshot/commit/d45e262b052e2f38e90dcbd1498fb68e8c92b91e))
* **builder:** constrain the Show details hit area ([080bf91](https://github.com/NotRexed/Hookshot/commit/080bf918ed7b857fb70cd463c4cd42b94c878e37)), closes [#1842](https://github.com/NotRexed/Hookshot/issues/1842)
* **builder:** give the prepare shaders tick its own row ([1ace933](https://github.com/NotRexed/Hookshot/commit/1ace933eea9ac440c9991594f7a3fc6686e172b2))
* **builder:** honor DLSS opt-in when updating a game ([#1643](https://github.com/NotRexed/Hookshot/issues/1643)) ([c3aa9c0](https://github.com/NotRexed/Hookshot/commit/c3aa9c075856f4219b1b4a0d3d280edd3ef8fa5f))
* **builder:** report bytes during NUP extraction ([#1671](https://github.com/NotRexed/Hookshot/issues/1671)) ([c4a9085](https://github.com/NotRexed/Hookshot/commit/c4a90851181f918a15842be8ab90e90d3ab7eb5b))
* **builder:** select the saved build memory in BeginUpdate ([d08aaed](https://github.com/NotRexed/Hookshot/commit/d08aaed17bb606c4aacd9f6b7bd5f296151de6f4))
* **builder:** the Linux builder's details box is read-only ([b2ac2bf](https://github.com/NotRexed/Hookshot/commit/b2ac2bf974d6c900160bbd585b1f588eef2315db))
* **builder:** the Linux builder's details box is read-only ([d7cc1d4](https://github.com/NotRexed/Hookshot/commit/d7cc1d48662c2c1a7eda65213cc67f7e6b588038))
* **build:** escape the build folder in glob patterns (a work folder with [ ] found no lifted code) ([#1722](https://github.com/NotRexed/Hookshot/issues/1722)) ([caaee0b](https://github.com/NotRexed/Hookshot/commit/caaee0ba6aef97514132603c9e6fa9e58f99bf57))
* **build:** invalidate DLSS objects when SDK headers change ([#1648](https://github.com/NotRexed/Hookshot/issues/1648)) ([c716137](https://github.com/NotRexed/Hookshot/commit/c716137334de9aa9b7f78d02438c45374f6fe9a2))
* **build:** package scripts/ in the Linux AppImage, so NVIDIA DLSS can be fetched ([b0aa6f6](https://github.com/NotRexed/Hookshot/commit/b0aa6f6f2936ed10d67d9af49512c9419c94173e))
* **build:** package scripts/ in the Linux AppImage, so NVIDIA DLSS can be fetched ([af9e98c](https://github.com/NotRexed/Hookshot/commit/af9e98c8d890fcd38b701ddc8f63b7e81a1dcde0))
* **build:** stop sampling memory for exited compiler roots ([#1562](https://github.com/NotRexed/Hookshot/issues/1562)) ([4a7ae1b](https://github.com/NotRexed/Hookshot/commit/4a7ae1b3d50d1da0bc4e4acfe538afcd37d431e8))
* **build:** the logo and fonts are made only where the Vulkan back end is compiled (an AppImage build stopped at LibertinusSans-Regular.ttf) ([#1567](https://github.com/NotRexed/Hookshot/issues/1567)) ([6add2f1](https://github.com/NotRexed/Hookshot/commit/6add2f17e551abdc8f65c0f4ce16093b04765a2b))
* Discord status starts on Windows (it was only started in the Linux build's main) ([#1539](https://github.com/NotRexed/Hookshot/issues/1539)) ([7d1562d](https://github.com/NotRexed/Hookshot/commit/7d1562d3c613e26fd0c4f7bd2265a36d0edc02b9))
* **display:** address pointer sampling review feedback ([acf9dae](https://github.com/NotRexed/Hookshot/commit/acf9daeeb7a4b68407a834cc3dcb5307a21d043f))
* **display:** do not toggle fullscreen on F11 when modifier keys are held ([#1309](https://github.com/NotRexed/Hookshot/issues/1309)) ([#1608](https://github.com/NotRexed/Hookshot/issues/1608)) ([635ba1a](https://github.com/NotRexed/Hookshot/commit/635ba1a7fa6cabb01aec34beb0a8d4e53390714f))
* **display:** integrate coherent game-menu pointer samples ([09685de](https://github.com/NotRexed/Hookshot/commit/09685de31e98ef4b6781f9b6b3196204186f05ee))
* **display:** integrate main and declare pointer fixture requirements ([8a6f1e5](https://github.com/NotRexed/Hookshot/commit/8a6f1e5a0a18b1542265ce0ad1c2b994862c2f56))
* **display:** open the window on COSMIC's Wayland with SDL2 ([#992](https://github.com/NotRexed/Hookshot/issues/992)) ([e937168](https://github.com/NotRexed/Hookshot/commit/e9371683cc3162c882cf0a37c06bc4b6f4e5e693))
* **display:** publish coherent pointer and drawable samples ([fd93173](https://github.com/NotRexed/Hookshot/commit/fd93173674025b01fa2588078617182009a340d3))
* **display:** publish coherent pointer and drawable samples ([ee53072](https://github.com/NotRexed/Hookshot/commit/ee530725bf8ef0ddacae2b409174d13b7b7b2dc6))
* **env:** hookshot_env.txt sets no test switch that names a file or presses buttons ([74cee1e](https://github.com/NotRexed/Hookshot/commit/74cee1e17173bd0e139d05a0a7613f2f4e57ee9d))
* **framegen:** 2x made by the renderer's own blend again; AMD FSR 3.1's interpolation opt-in (RX_VK_FSR_FG=1) ([eb5b9a0](https://github.com/NotRexed/Hookshot/commit/eb5b9a0b0bfb2da861c131cfd0bdb613c279d09e))
* **framegen:** 2x uses the renderer's own blend again; FSR 3.1 interpolation opt-in ([#1635](https://github.com/NotRexed/Hookshot/issues/1635)) ([3b76e95](https://github.com/NotRexed/Hookshot/commit/3b76e956fed4499c02a6cd9e5afa9093bb3dfc1e))
* hardening part 3, updates, shared files, guest buffers and texture sizes ([d5feb7b](https://github.com/NotRexed/Hookshot/commit/d5feb7b875e77d5df9f9a577b6ff61090d691500))
* **hid:** deduplicate gamepad polling when SDL controllers are active ([#1480](https://github.com/NotRexed/Hookshot/issues/1480), [#1583](https://github.com/NotRexed/Hookshot/issues/1583)) ([2b0f762](https://github.com/NotRexed/Hookshot/commit/2b0f762784dd39e76d61949f58e7d877a7f04018))
* **hid:** deduplicate gamepad polling when SDL controllers are active ([#1480](https://github.com/NotRexed/Hookshot/issues/1480), [#1583](https://github.com/NotRexed/Hookshot/issues/1583)) ([731898a](https://github.com/NotRexed/Hookshot/commit/731898a24be449efc2165e99695d8473e42ef97d))
* **hos:** host copies, file reads and writes, and the NV12 reader stay in guest memory ([6f8f243](https://github.com/NotRexed/Hookshot/commit/6f8f243285babe32c8ae4ff16ae4e3aa85097a12))
* **host:** room in the drivers' shader caches for the prepared pipelines (Linux) ([#2033](https://github.com/NotRexed/Hookshot/issues/2033)) ([c9ec3bb](https://github.com/NotRexed/Hookshot/commit/c9ec3bb809c319bcf86981d1adca3b2ffe281ca1))
* **host:** the cache cores only where the L3s are shared alike and the largest has 8 cores (review of [#1638](https://github.com/NotRexed/Hookshot/issues/1638)) ([428633a](https://github.com/NotRexed/Hookshot/commit/428633a908b0e54840e2f0a583db75ebdd499106))
* **input:** a stick axis steers once it has been seen at rest (camera spinning on its own, [#1673](https://github.com/NotRexed/Hookshot/issues/1673), [#1661](https://github.com/NotRexed/Hookshot/issues/1661)) ([#1677](https://github.com/NotRexed/Hookshot/issues/1677)) ([d8f0513](https://github.com/NotRexed/Hookshot/commit/d8f0513f4555d3f6fa2a6d2df6fa76015ee17dbc))
* **linux:** safely rebase legacy sysroot links ([#1553](https://github.com/NotRexed/Hookshot/issues/1553)) ([ca3a252](https://github.com/NotRexed/Hookshot/commit/ca3a2529caac6e59c4d60f94a77afd275a331137))
* **loader:** add the addend to GLOB_DAT and JUMP_SLOT relocations ([1372edb](https://github.com/NotRexed/Hookshot/commit/1372edbb0dde160b3b189ed0ed20c722aeb1461f))
* **loader:** add the addend to GLOB_DAT and JUMP_SLOT relocations ([95a9a07](https://github.com/NotRexed/Hookshot/commit/95a9a073d3deaf071c7f21366dd9cc9c253c66db))
* **make_game:** --prepare says errors and --check on stdout, and refuses a missing --game ([2d27d08](https://github.com/NotRexed/Hookshot/commit/2d27d08f651b4846361df03a622a716be7b89cf1))
* **menu:** A and B confirm and go back on a controller, as in the game ([02b0903](https://github.com/NotRexed/Hookshot/commit/02b0903b128469b7f1c8ee652a681c4f161e802e))
* **menu:** a save keeps the options file's lines the menu does not write ([#1493](https://github.com/NotRexed/Hookshot/issues/1493)) ([a2e6505](https://github.com/NotRexed/Hookshot/commit/a2e650526696843506e17393f6632349eadfb995))
* **menu:** a size from the config shows as its size alone, no label ([bc9406c](https://github.com/NotRexed/Hookshot/commit/bc9406cb6179dd4e89f96973444f4bd5a8839654))
* **menu:** clarify frame-generation preset help ([#1554](https://github.com/NotRexed/Hookshot/issues/1554)) ([77685a5](https://github.com/NotRexed/Hookshot/commit/77685a582e08286b1be363b22749fb38ef9debd5))
* **menu:** distinguish game and generated frame rates ([#1659](https://github.com/NotRexed/Hookshot/issues/1659)) ([b2bb214](https://github.com/NotRexed/Hookshot/commit/b2bb214465d813e3609f9602ad46f1a2163f34c1))
* **menu:** F1 always opens the menu (Menu shortcut: Insert or F1, or F1 only) ([#1632](https://github.com/NotRexed/Hookshot/issues/1632)) ([d448ccc](https://github.com/NotRexed/Hookshot/commit/d448ccc988082de44360f26fb47a497f76644048))
* **menu:** Frame generation is the player's choice; Quality presets leave it alone ([2ef499d](https://github.com/NotRexed/Hookshot/commit/2ef499d8dedb827a6eaed6f58f2d28c9e5f4f32d))
* **menu:** hide rows for features this PC lacks, and keep mouse clicks on the row meant ([51de681](https://github.com/NotRexed/Hookshot/commit/51de6815c447c898072a079318fe0389e093d1d5))
* **menu:** hide rows for features this PC lacks, and keep mouse clicks on the row meant ([849b63e](https://github.com/NotRexed/Hookshot/commit/849b63eed80927572e9e72925dd53fde14250cf3))
* **menu:** keep the FPS counter off the banner on a window just wider than 16:9 ([#1545](https://github.com/NotRexed/Hookshot/issues/1545)) ([50d71bd](https://github.com/NotRexed/Hookshot/commit/50d71bd7186bf5e31574c5f9f262fac382bc2f42))
* **menu:** keep the help box and rows above the prompt strip ([bd1616f](https://github.com/NotRexed/Hookshot/commit/bd1616f546823b2c49eaa3b36b01e70e8099f45e))
* **menu:** Menu size is Medium on every input, and the version shrinks to fit ([73feec0](https://github.com/NotRexed/Hookshot/commit/73feec01dc582e63d9db95ecce6baa5f20855a80))
* **menu:** Quality presets leave Frame generation alone, so it stays off by default ([a72690c](https://github.com/NotRexed/Hookshot/commit/a72690c9bc375fcee2a0664425ff19288d1b9528))
* **menu:** run the upscalers' one-time probes during the start-up screen, not at the menu's first open ([#1886](https://github.com/NotRexed/Hookshot/issues/1886)) ([#1916](https://github.com/NotRexed/Hookshot/issues/1916)) ([74075de](https://github.com/NotRexed/Hookshot/commit/74075deb96e212bfecca2271f198c86309b7a18e))
* **menu:** the FPS counter looks as it did (green outlined text in the corner, no plate) ([#1957](https://github.com/NotRexed/Hookshot/issues/1957)) ([92907f7](https://github.com/NotRexed/Hookshot/commit/92907f7fc4ebabb990d64583574216da1859f37a))
* **menu:** the mouse wheel scrolls, it never changes a value ([#1557](https://github.com/NotRexed/Hookshot/issues/1557)) ([be541fa](https://github.com/NotRexed/Hookshot/commit/be541fa7d5a499cbda0b30219a13c7ef0337b6ce))
* **nup:** name the member and the drive when extraction runs out of space ([#1595](https://github.com/NotRexed/Hookshot/issues/1595)) ([218d8de](https://github.com/NotRexed/Hookshot/commit/218d8dec97e8c5bc4ace6324928be4ed769488d3))
* **packaging:** include the prompt encoder's runtime dependency ([#1598](https://github.com/NotRexed/Hookshot/issues/1598)) ([53a5fd5](https://github.com/NotRexed/Hookshot/commit/53a5fd5c1beb44b2ad74b1ca7b938df8a42a6053))
* preserve UTF-8 and extraction guards in long-path support ([#1592](https://github.com/NotRexed/Hookshot/issues/1592)) ([38a5794](https://github.com/NotRexed/Hookshot/commit/38a579461a4ceb3c2285921beaad3ac9f8121ed9))
* **render:** publish coherent shared temporal jitter samples ([a38a07d](https://github.com/NotRexed/Hookshot/commit/a38a07d32604577351e2faeac900e5dbd06da1af))
* **render:** publish coherent temporal jitter samples ([0291727](https://github.com/NotRexed/Hookshot/commit/0291727a93572cf0fdb787d25578400a9865312c))
* **render:** reset temporal history when reprojection is invalid ([0cf9936](https://github.com/NotRexed/Hookshot/commit/0cf9936c012c7c8067f007e89995b243719fc8c3))
* **render:** reset temporal history when reprojection is invalid ([19b81b7](https://github.com/NotRexed/Hookshot/commit/19b81b7d789627278bc67396a50354a1622cb453))
* **rt:** mark an exclusive store's page after the store, not before ([7cb4028](https://github.com/NotRexed/Hookshot/commit/7cb4028ca466d52cf6a72d85c2b006b765305cf4))
* **rt:** mark an exclusive store's page after the store, not before ([db6324f](https://github.com/NotRexed/Hookshot/commit/db6324f6c2236bb245b90b6a15a92a3abf4442fe))
* **scripts:** the profile readers name and count Linux samples ([#1746](https://github.com/NotRexed/Hookshot/issues/1746)) ([6269088](https://github.com/NotRexed/Hookshot/commit/6269088c1c28793d7df0c5d086d90e01e7b4bcad))
* **security:** a font's cmap offset, a microphone's name, and the game's library loads stay in bounds ([9323390](https://github.com/NotRexed/Hookshot/commit/9323390e033dede43acfd668ec95f5d2bdac6f50))
* **security:** verify Windows updater asset digests ([6d5f277](https://github.com/NotRexed/Hookshot/commit/6d5f2774ccb49dcb9924bb7b04eb36361d24366d))
* **security:** verify Windows updater asset digests ([50b6866](https://github.com/NotRexed/Hookshot/commit/50b68667eb5bc7ca23dce8f440835b3d0e429533)), closes [#1679](https://github.com/NotRexed/Hookshot/issues/1679)
* **setup:** explain how to restore missing Windows tar ([1c1995e](https://github.com/NotRexed/Hookshot/commit/1c1995e0208f2cf52c3c119a5196fd11481c23f9))
* **setup:** explain recovery when Windows tar is missing ([fbaf722](https://github.com/NotRexed/Hookshot/commit/fbaf722bd5cd75f28a0e92314d1d2ea84dd19170))
* **shaders:** a loop that leaves by predicated EXITs ends at its closing self-branch (the last program that did not lower) ([a8e7ee5](https://github.com/NotRexed/Hookshot/commit/a8e7ee55ad522400deb0e5c1f572d25adeba759e))
* **shaders:** a program ends at its closing self-branch after an unconditional jump back ([7e1db37](https://github.com/NotRexed/Hookshot/commit/7e1db3722c01ce5bac6c6e66b5c27a24c26ae556))
* **shaders:** an immediate's low byte is not a scratch register ([d0f3511](https://github.com/NotRexed/Hookshot/commit/d0f35116cd5501a88cdc15fb4ac60b6f1a7b8044))
* **shaders:** ISBERD.O, a task or mesh program reading back its output region ([67c048e](https://github.com/NotRexed/Hookshot/commit/67c048e0686e63d413beeeb0a6b6917995ffc50c))
* **shaders:** ISBERD.O, a task program reading back its payload (12 failing pipelines) ([0904155](https://github.com/NotRexed/Hookshot/commit/09041557d48f75398fb29443a7bc9281d3b5e06e))
* **shaders:** LDG [URn+imm] with both ordering bits 77-78 lowers ([692a1c0](https://github.com/NotRexed/Hookshot/commit/692a1c0f7d3e2dbcec8e20cae76bf3ff9a1a9577))
* **shaders:** LDG/STG [URn+imm] with ordering bit 78 lower (2 compute programs) ([5ec9668](https://github.com/NotRexed/Hookshot/commit/5ec9668b25a2c3bb8ba251fc4f2fe63fd636fdf2))
* **shaders:** lower TLD.MS (multisampled texel fetch) to Maxwell TLDS 2D.LZ.MS ([6860451](https://github.com/NotRexed/Hookshot/commit/68604511475d32b9c58f5013de682328c977a070))
* **shaders:** lower TLD.MS, a multisampled texture's texel, to Maxwell TLDS 2D.LZ.MS ([20dd78b](https://github.com/NotRexed/Hookshot/commit/20dd78bb58e6cea23be3cc43eaaa36909276c6c6))
* **shaders:** R2UR's source is one register, not a pair ([597edcb](https://github.com/NotRexed/Hookshot/commit/597edcb379962827624c306c48c4ba9511508a86))
* **shaders:** R2UR's source is one register, not a pair (a vertex program refused) ([9edb3a5](https://github.com/NotRexed/Hookshot/commit/9edb3a57e73f1e6351f53b7fdefe858a04c98117))
* **shaders:** STG [URn+imm] with ordering bit 78 lowers too ([923a334](https://github.com/NotRexed/Hookshot/commit/923a334dc7d3bd8cc4c849e8301a8579bea9eb82))
* **shaders:** two large compute programs lower (an immediate is not R254) and get their local memory ([10cfd70](https://github.com/NotRexed/Hookshot/commit/10cfd701c4f635095a82249d3d64b392589005a4))
* the display stays on while playing with a controller ([#1532](https://github.com/NotRexed/Hookshot/issues/1532)) ([#1538](https://github.com/NotRexed/Hookshot/issues/1538)) ([c38df20](https://github.com/NotRexed/Hookshot/commit/c38df20396a7f4cdc5d063b93bf822b90caad5b3))
* **update:** Linux downloads are GETs again; a part's links stay in the install; the requested tag is installed ([df9a52e](https://github.com/NotRexed/Hookshot/commit/df9a52e3f777b823867c49d6c33b14cf2f338866))
* **vi:** a cutscene's cues stay with its movie above a 60 cap ([a7cc747](https://github.com/NotRexed/Hookshot/commit/a7cc747d5fdb73b50ab9da19441876d8b97a4737))
* **vi:** a cutscene's cues stay with its movie above a 60 cap ([#1742](https://github.com/NotRexed/Hookshot/issues/1742)) ([6c596af](https://github.com/NotRexed/Hookshot/commit/6c596af6c2667c8e095e6620ce896430f6465803))
* **vi:** keep the game at real speed under a Frame rate cap above 60 ([#1612](https://github.com/NotRexed/Hookshot/issues/1612)) ([1a429f6](https://github.com/NotRexed/Hookshot/commit/1a429f6a84e3dd53fd4304e85abd0356da4164ca))
* **vk:** a masked clear that backed off is tried again now and then ([#1787](https://github.com/NotRexed/Hookshot/issues/1787)) ([fa04b6f](https://github.com/NotRexed/Hookshot/commit/fa04b6fcab179eb51062fe57d0e3fd8a861f1b21))
* **vk:** a masked clear whose pipeline the driver did not make is tried again, not cached as null ([#1787](https://github.com/NotRexed/Hookshot/issues/1787)) ([9d94f4c](https://github.com/NotRexed/Hookshot/commit/9d94f4c4cee70787ee9b468edf1a698aff2e5d04))
* **vk:** a masked clear whose pipeline the driver did not make is tried again, not cached as null ([#1787](https://github.com/NotRexed/Hookshot/issues/1787)) ([f002c90](https://github.com/NotRexed/Hookshot/commit/f002c9048991003b7f4e5253a6a55403f9d83141))
* **vk:** a shadow is made only where the textures' own lines are not passed (review of [#1637](https://github.com/NotRexed/Hookshot/issues/1637)) ([ecb6e9a](https://github.com/NotRexed/Hookshot/commit/ecb6e9ad2b5a4191b1c9593db44326679ca9d45f))
* **vk:** a switch for an engine that cannot run leaves a saved FSR 3.1 ([#1773](https://github.com/NotRexed/Hookshot/issues/1773) review) ([a9ea73a](https://github.com/NotRexed/Hookshot/commit/a9ea73a5e6dae75684762225e3ceb72feddd5d0f))
* **vk:** a texture's storage size is computed in 64 bits, and one past the GPU's limits is not made ([81a6da5](https://github.com/NotRexed/Hookshot/commit/81a6da550f8b044fbfcd2c5bc8da58db792508d4))
* **vk:** add VMA image allocation retry without WITHIN_BUDGET ([39d978b](https://github.com/NotRexed/Hookshot/commit/39d978b49f6b5f806a55c0da01c12b258ebb7674))
* **vk:** add VMA image allocation retry without WITHIN_BUDGET ([560cc5a](https://github.com/NotRexed/Hookshot/commit/560cc5af3ecd841a33db43beefa0984df9dfd8e0))
* **vk:** admit only successful samplers across cache layers ([52ac35c](https://github.com/NotRexed/Hookshot/commit/52ac35ca58a09187f2f261eecec0ab23328deab2))
* **vk:** admit only successful samplers across cache layers ([3379314](https://github.com/NotRexed/Hookshot/commit/3379314c5e486e67d018e7db2628b484eeffb9d5))
* **vk:** ask for task and mesh shader counts only where the driver offers them ([ba86aee](https://github.com/NotRexed/Hookshot/commit/ba86aeeeddd207e40d65fc6518d721c7d148f834))
* **vk:** ask for task and mesh shader counts only where the driver offers them ([a3d88ea](https://github.com/NotRexed/Hookshot/commit/a3d88ea5e14ac3ae3858b78c84d64a1ab4a5c65e))
* **vk:** ask the pipeline cache for a compute pipeline before queueing its build ([0db1e74](https://github.com/NotRexed/Hookshot/commit/0db1e746ab7d4db2ce8901077a1a47028540b913))
* **vk:** avoid aligned uniform-word cache collisions ([1b25b22](https://github.com/NotRexed/Hookshot/commit/1b25b22ce91947d6c16c8688a6f22e5e52d90430))
* **vk:** build every predicted pipeline before the game (the prediction found nothing on fresh builds; no 90 s budget; one progress bar) ([#2032](https://github.com/NotRexed/Hookshot/issues/2032)) ([ff85d7d](https://github.com/NotRexed/Hookshot/commit/ff85d7d1042fdfe88d06717ae35ff7b5ed59698d))
* **vk:** build the precompile's first graphics pipeline alone ([41b3438](https://github.com/NotRexed/Hookshot/commit/41b343827ab59b8817492e370e415e33fe090d71))
* **vk:** capture jitter cache frame under its lock ([1f32a16](https://github.com/NotRexed/Hookshot/commit/1f32a16311f4a6c9dc950053a0b821b345caf608))
* **vk:** check cached descriptor counts and the formats of noted pipelines ([59f9284](https://github.com/NotRexed/Hookshot/commit/59f9284e4faaab2ddc5509cf1105e0d7f7e094c3))
* **vk:** coloured specks on fallback foliage (AMD): the generated vertex stage's outputs start at (0,0,0,1) ([16631b6](https://github.com/NotRexed/Hookshot/commit/16631b6a24a64d9454d3819242043d852802955a))
* **vk:** commit bank cache generations after successful stream upload ([60ea984](https://github.com/NotRexed/Hookshot/commit/60ea98411b1b14eb83560b9a6b8c8deff3f6afa0))
* **vk:** compute programs get their local memory size (control section +0x68) ([a083825](https://github.com/NotRexed/Hookshot/commit/a0838250a913a0117cac63b86df28d554ee4d4cf))
* **vk:** dark lighting and glowing bloom on AMD: 64-lane graphics warps and Lichii [#273](https://github.com/NotRexed/Hookshot/issues/273)'s fragment shuffle fallback ([030e605](https://github.com/NotRexed/Hookshot/commit/030e6058eb3b09f9b827f5dc31eb7170e1a769f9))
* **vk:** decode Deck gameplay upscale as RGB9E5 ([903bc4a](https://github.com/NotRexed/Hookshot/commit/903bc4a603fc5a4260902394c8e4bf305d61bdfc))
* **vk:** decode gameplay RGB9E5 upscale fallback ([6c936d4](https://github.com/NotRexed/Hookshot/commit/6c936d4a966a609e80a3b198ef8fb81b5ca05862))
* **vk:** dense grass cut off and far trees flickering without mesh shaders: fallback records from an allocator, 65535 slots ([7047756](https://github.com/NotRexed/Hookshot/commit/70477565070871f63892caea0a547d15bacd7ccc))
* **vk:** disable failed frame log sinks and unwind startup ([#1531](https://github.com/NotRexed/Hookshot/issues/1531)) ([5fde3cf](https://github.com/NotRexed/Hookshot/commit/5fde3cf85b7e64fa3e18d56911080d683d854331))
* **vk:** establish capture copy visibility for host reads ([7ef03d4](https://github.com/NotRexed/Hookshot/commit/7ef03d4730e60d9b8272f1c4ebb1fb2a4b5c21d6))
* **vk:** expose the retirement helper across renderer units ([d5b7437](https://github.com/NotRexed/Hookshot/commit/d5b743799196609524c308e947cfdabf4b1099a1))
* **vk:** flush custom UI texture staging before GPU use ([#1559](https://github.com/NotRexed/Hookshot/issues/1559)) ([188baa6](https://github.com/NotRexed/Hookshot/commit/188baa6f431b19cbda92beaa6ab6b108f44b3c4a))
* **vk:** invalidate capture readbacks before CPU reads ([0c6c047](https://github.com/NotRexed/Hookshot/commit/0c6c04783a221aa795d9747e63cbd2d78649f180))
* **vk:** keep adjacent uniform words in separate cache slots ([7b81752](https://github.com/NotRexed/Hookshot/commit/7b817526d2e459a4881cac09cb4f9cfe84fd82fb))
* **vk:** keep Windows prepare times in FILETIME ticks ([#1517](https://github.com/NotRexed/Hookshot/issues/1517)) ([edeaa9d](https://github.com/NotRexed/Hookshot/commit/edeaa9dba810e6952ce5d651812789c928f9c300))
* **vk:** let a draw run its own queued shader translation ([#1714](https://github.com/NotRexed/Hookshot/issues/1714)) ([8bac8aa](https://github.com/NotRexed/Hookshot/commit/8bac8aa67dc1260e18cb1bf50e844b0d943967eb))
* **vk:** log a driver fault caught by the Windows pipeline guards ([ac7d4a7](https://github.com/NotRexed/Hookshot/commit/ac7d4a74250929d966dd5bce1966e74217f4e120))
* **vk:** make capture readbacks visible before CPU reads ([6e06af5](https://github.com/NotRexed/Hookshot/commit/6e06af54e49cf3c840eb6d4282bd7c024dcffb9b))
* **vk:** make frame dump copies visible to host reads ([#1621](https://github.com/NotRexed/Hookshot/issues/1621)) ([c1a4fde](https://github.com/NotRexed/Hookshot/commit/c1a4fde2ef4562c75a9e0e89f7895b0ad7dbbdcd))
* **vk:** mark a clear's targets in the parallel replay's dry pass (the one-frame black scenes) ([adb7a5d](https://github.com/NotRexed/Hookshot/commit/adb7a5dd7025e904d09cf2a4451603fd8f961aeb))
* **vk:** missing floor grass without mesh shaders: indirect task draws ran no mesh workgroups ([ccf7f41](https://github.com/NotRexed/Hookshot/commit/ccf7f4125409c93970ec1e426d7a8f9b93dd5cc9))
* **vk:** own native FSR constants for each recording ([2982420](https://github.com/NotRexed/Hookshot/commit/2982420d3ff07eafd67f3ca196cb65c25ec0ccf8))
* **vk:** own native FSR constants for each recording ([895d673](https://github.com/NotRexed/Hookshot/commit/895d6731156011cde4040a0ea678b5182e905414))
* **vk:** parallel replay: a clear's targets are marked in the dry pass (the one-frame black scenes) ([168e4d1](https://github.com/NotRexed/Hookshot/commit/168e4d1959df12465588d511f55ddec29759465e))
* **vk:** present ahead's vk_replay.cpp half, lost in [#1720](https://github.com/NotRexed/Hookshot/issues/1720)'s merge ([be8b512](https://github.com/NotRexed/Hookshot/commit/be8b512c0109600643036a201e49942c35f49901))
* **vk:** present ahead's vk_replay.cpp half, lost in [#1720](https://github.com/NotRexed/Hookshot/issues/1720)'s merge (main doesn't link) ([1392ea3](https://github.com/NotRexed/Hookshot/commit/1392ea3c2442964cde3e0fb6641db2785008b473))
* **vk:** preserve brightness in flash sampling across capture sizes ([#1537](https://github.com/NotRexed/Hookshot/issues/1537)) ([6f5ecab](https://github.com/NotRexed/Hookshot/commit/6f5ecab9b8f56527c08f6075cc863dc738e9396b))
* **vk:** preserve contiguous words in the mixed uniform cache ([70ffaf2](https://github.com/NotRexed/Hookshot/commit/70ffaf20b535fdfc37a9a2a2a391712ba4ed31a4))
* **vk:** preserve CPU write watches during pool imports ([#1434](https://github.com/NotRexed/Hookshot/issues/1434)) ([74dcb75](https://github.com/NotRexed/Hookshot/commit/74dcb759b4a9f8c018f972da67d933cbc69312d0))
* **vk:** prevent aligned uniform words from thrashing the cache ([78e1112](https://github.com/NotRexed/Hookshot/commit/78e1112d70d8124a8009d67a97a126ef31390b8b))
* **vk:** publish complete PNG captures and report failures ([#1604](https://github.com/NotRexed/Hookshot/issues/1604)) ([ba40a1a](https://github.com/NotRexed/Hookshot/commit/ba40a1abf11d4b12bdfff4f92a06881eaa64950b))
* **vk:** publish complete screenshot resource sets ([#1472](https://github.com/NotRexed/Hookshot/issues/1472)) ([ff69134](https://github.com/NotRexed/Hookshot/commit/ff691348b002da90b3444c4be05939fbc9fd7874))
* **vk:** publish recording stream writes before GPU submission ([d71739d](https://github.com/NotRexed/Hookshot/commit/d71739d255f38cffa5b8768993edead67eafdaf9))
* **vk:** publish recording stream writes before GPU submission ([0e843bd](https://github.com/NotRexed/Hookshot/commit/0e843bde963d5810aef3ab23629b1d0d7447b0da))
* **vk:** publish texture staging slices before GPU reads ([#1672](https://github.com/NotRexed/Hookshot/issues/1672)) ([a6e2e14](https://github.com/NotRexed/Hookshot/commit/a6e2e14e64898150a3655bf4d0dd6f58614d109f))
* **vk:** Read texture handles from stage constant banks ([31933e4](https://github.com/NotRexed/Hookshot/commit/31933e4e93137c6983c685ddef2d5a80b18ea68d))
* **vk:** Read texture handles from stage constant banks ([4654870](https://github.com/NotRexed/Hookshot/commit/4654870235bef8986531485a71e0a90257cb8bf1))
* **vk:** recover recording stream and descriptor allocation failures ([fad4d16](https://github.com/NotRexed/Hookshot/commit/fad4d1691062fe59a144968afe7d42348708f9ba))
* **vk:** recover recording stream and descriptor allocation failures ([6e93f80](https://github.com/NotRexed/Hookshot/commit/6e93f80ffac5747213b6e584e1d08cc946cea2d0))
* **vk:** reject failed descriptor-pool creation results ([6a51fc2](https://github.com/NotRexed/Hookshot/commit/6a51fc2e82a8b1e86acb063b4d91b23d8269f7fa))
* **vk:** reject failed samplers before descriptor publication ([2914a73](https://github.com/NotRexed/Hookshot/commit/2914a73cada37f2bf619e318d94fc4936947cf9a))
* **vk:** reject missing RGB9E5 fallback views ([6f5f769](https://github.com/NotRexed/Hookshot/commit/6f5f769fd46953cf771c64b4e9aa306533162a01))
* **vk:** reject unbound imported buffers and roll back ownership ([325583b](https://github.com/NotRexed/Hookshot/commit/325583b981f8fed79b99cb10b1b03bc0e710ee75))
* **vk:** reject unbound imported buffers and roll back ownership ([af8f012](https://github.com/NotRexed/Hookshot/commit/af8f012581108ae21cdf050d76ab839d934d56dd))
* **vk:** replace menu upload buffers only after successful growth ([#1453](https://github.com/NotRexed/Hookshot/issues/1453)) ([60b0c9b](https://github.com/NotRexed/Hookshot/commit/60b0c9b077c44c874d355621781d36297557278d))
* **vk:** report device loss throughout native presentation ([5997b2d](https://github.com/NotRexed/Hookshot/commit/5997b2d8303904a49e6994312ece69321989e576))
* **vk:** report device loss throughout native presentation ([8e1abe3](https://github.com/NotRexed/Hookshot/commit/8e1abe38b1816edd38c2b1d70f55e3504be12ed0))
* **vk:** reset swap_spare with the swapchain; leave a skipped present out of the counts ([08f3fe4](https://github.com/NotRexed/Hookshot/commit/08f3fe4ce5676817bb4441c00926badafc1dfc9c))
* **vk:** retire native FSR images after GPU completion ([8fc2587](https://github.com/NotRexed/Hookshot/commit/8fc2587c7d7a7232286dab3b9fb24f5bd7f19d1e))
* **vk:** retire native FSR images after GPU completion ([4aaf336](https://github.com/NotRexed/Hookshot/commit/4aaf336b98975c5043ba12a2adb8c27d40157fad))
* **vk:** retry transient texture image view failures ([8686552](https://github.com/NotRexed/Hookshot/commit/86865527d540fdc2890910be2c173a14f2d30d32))
* **vk:** retry transient texture image view failures ([394b797](https://github.com/NotRexed/Hookshot/commit/394b797e1c462c6a46ab9cd278f9e6010bfcfdf6))
* **vk:** RX_VK_FSR4 and RX_VK_DLSS go over a saved FSR 3.1 ([#1008](https://github.com/NotRexed/Hookshot/issues/1008) follow-up) ([4cde5c6](https://github.com/NotRexed/Hookshot/commit/4cde5c6ff515a1f213a4e279ba7580ac958bd3cc))
* **vk:** RX_VK_FSR4 and RX_VK_DLSS go over a saved FSR 3.1 ([#1008](https://github.com/NotRexed/Hookshot/issues/1008) review) ([6229c02](https://github.com/NotRexed/Hookshot/commit/6229c02dea2fc8bc1696ae9bb218427cbede23ad))
* **vk:** RX_VK_VRAM_MB and the integrated-GPU cap win over a preset's texture budget ([#1376](https://github.com/NotRexed/Hookshot/issues/1376)) ([e27740a](https://github.com/NotRexed/Hookshot/commit/e27740a608b9ab9d5f89da7b2c1bd26af6964bb1))
* **vk:** share validated source crops across presentation paths ([187e70b](https://github.com/NotRexed/Hookshot/commit/187e70b95c96837e34bdddca91177384b8bdf0bc))
* **vk:** share validated source crops across presentation paths ([d4c0429](https://github.com/NotRexed/Hookshot/commit/d4c0429258131f0731afdcd7853de051afe5d94e))
* **vk:** shared_pipeline_cache outside the unnamed namespace (vk_fsr3.cpp could not link to it) ([9d44621](https://github.com/NotRexed/Hookshot/commit/9d44621b2d6914feb10a1cd4a7f18d2df646fc9e))
* **vk:** slow resume and black HUD hearts on 4 GB cards: the texture squeeze waits while VMA's blocks have room ([189287f](https://github.com/NotRexed/Hookshot/commit/189287f4047ca369cca4f6d4bd74f6d0d3972145))
* **vk:** snapshot pending shots before reusing their readback slot ([#1519](https://github.com/NotRexed/Hookshot/issues/1519)) ([aaf6ac1](https://github.com/NotRexed/Hookshot/commit/aaf6ac1b588ed72b6ea256691b15ab564e5c1381))
* **vk:** submit a 2x set's copies together so its presents keep their times ([64cfe26](https://github.com/NotRexed/Hookshot/commit/64cfe2600efc5f5308ed521e088eeac1dc170a0c))
* **vk:** submit a 2x set's copies together so its presents keep their times ([79e42ac](https://github.com/NotRexed/Hookshot/commit/79e42ac46834fa6646a8478fe5f95ebed18275a6))
* **vk:** the upscale stand-in and the texture clear call texture_written as before ([73c6dd3](https://github.com/NotRexed/Hookshot/commit/73c6dd3ba9d1f126690b3a0937b23842166dbdea))
* **vk:** tie texel buffer views to their imported backing ([692e87d](https://github.com/NotRexed/Hookshot/commit/692e87df5e31637c889e005323bf1c22481515c5))
* **vk:** tie texel buffer views to their imported backing ([a23587f](https://github.com/NotRexed/Hookshot/commit/a23587f5fa241017c3f4fdeec4a1559f7f20f6cf))
* **vk:** Timestamp frame captures with the host clock ([3563479](https://github.com/NotRexed/Hookshot/commit/3563479ec112cbac49b0d32a993ac4062078f29b))
* **vk:** Use host timestamps in frame captures ([b2ed731](https://github.com/NotRexed/Hookshot/commit/b2ed731f35aa66d63d74bb30e2580d0943b0dbd2))
* **vk:** validate capture dimensions before allocating readbacks ([#1551](https://github.com/NotRexed/Hookshot/issues/1551)) ([17e8cd8](https://github.com/NotRexed/Hookshot/commit/17e8cd833e55aaecdeec715dbd50e0f859e5f3e6))
* **vk:** validate HDR crops through the shared source rectangle ([f20afae](https://github.com/NotRexed/Hookshot/commit/f20afae95120c090c4c08802db9443248cdda9a4))
* **vulkan:** avoid the AMD cloth pipeline optimizer hang ([bebe51e](https://github.com/NotRexed/Hookshot/commit/bebe51e35bb672464ebc8369c3f78b3e0b1e6bcc))
* **vulkan:** finish the RX 6700 XT merge resolution ([5ffdaeb](https://github.com/NotRexed/Hookshot/commit/5ffdaeb4024ee8323a50b4738c353890819807d8))
* **vulkan:** refresh direct-write tracking on current main ([a4c8f85](https://github.com/NotRexed/Hookshot/commit/a4c8f85bf1e461796c43361386a0deac5b629f9c))
* **vulkan:** stop distant trees flickering in the mesh fallback ([7fc0a61](https://github.com/NotRexed/Hookshot/commit/7fc0a61fb2e3e413c478a52de5c0cf3d8b68b702))
* **vulkan:** track writes from direct buffer bindings ([be1aabc](https://github.com/NotRexed/Hookshot/commit/be1aabcf7755db814ef19eee5e9d656ea0dd02e8))
* write-back stale-shadow edgecase ([c5aa906](https://github.com/NotRexed/Hookshot/commit/c5aa90631c90a116cb30fcfac83adbdf0bd681b2))


### Performance Improvements

* **host:** keep the game's threads on the 3D V-Cache cores of two-CCD X3D CPUs ([1360a44](https://github.com/NotRexed/Hookshot/commit/1360a44eadeefd70870f13526161c5533570376b))
* **host:** on X3D CPUs with two CCDs the game's threads stay on the cores that share the 3D V-Cache ([b3c0982](https://github.com/NotRexed/Hookshot/commit/b3c09823bc8dec289af7ea39028cbeeb07176d08))
* **host:** write the log in batches, a few writes a second instead of one per line ([c7e41fd](https://github.com/NotRexed/Hookshot/commit/c7e41fdf662f823027685dfd0d97e6f9fa61ce9e))
* **host:** write the log in batches, a few writes a second instead of one per line ([7f12643](https://github.com/NotRexed/Hookshot/commit/7f126435601970031a974e1bbc37e2da96ac40cb))
* **vk:** a captured page's words read from the capture, not a 4 KB copy kept a thread ([0b951ec](https://github.com/NotRexed/Hookshot/commit/0b951ec7f9afeb6c3a06770f823546d56ad8ba6c))
* **vk:** copy captured pages with non-temporal stores ([d8ce191](https://github.com/NotRexed/Hookshot/commit/d8ce1918d550930c3206125cee1be2ff703e62e0))
* **vk:** copy captured pages with non-temporal stores ([146fe38](https://github.com/NotRexed/Hookshot/commit/146fe384815a6f52115d6b6ed4ed0eac7d6f003f))
* **vk:** Descriptor reuse on by default (options_version 4) ([f758d9f](https://github.com/NotRexed/Hookshot/commit/f758d9f3a146fce52924525e36d5171d3fb09066))
* **vk:** Descriptor reuse on by default (options_version 4) ([f214c0c](https://github.com/NotRexed/Hookshot/commit/f214c0c403d967eb0e55acb00d32024f137f6b35))
* **vk:** DLSS keeps its feature through loading screens while it is the Upscaler (a 44-78 ms stutter at each area entry) ([#1520](https://github.com/NotRexed/Hookshot/issues/1520)) ([549a43b](https://github.com/NotRexed/Hookshot/commit/549a43b5a80fc9ca55f00b990fc953a860a69b77))
* **vk:** flush only each FSR uniform slice ([c7c88fa](https://github.com/NotRexed/Hookshot/commit/c7c88fa2044c0e63e632222b6f4a3e7c03772170))
* **vk:** FSR 3.1's passes go through the pipeline cache kept across runs ([e805e02](https://github.com/NotRexed/Hookshot/commit/e805e02d7b382dacd9a32e2093284d7211aab464))
* **vk:** FSR 3.1's passes go through the pipeline cache kept across runs (a 1.7 s longer first frame of play) ([b27d9b6](https://github.com/NotRexed/Hookshot/commit/b27d9b6948c2f02495c470662d0b3344e49181a7))
* **vk:** FSR 3.1's pipelines through the play caches too (shared_compute_pipeline) ([46efc77](https://github.com/NotRexed/Hookshot/commit/46efc772111e59a3ea3fbefb6a6e0a79158e29b4))
* **vk:** in play an ask without compiling goes through the play caches too ([#1765](https://github.com/NotRexed/Hookshot/issues/1765)'s dispatch, the stage libraries) ([6593de7](https://github.com/NotRexed/Hookshot/commit/6593de70d877bfe178599bc50a23d04f00d94ce8))
* **vk:** keep 32 submitted pages a thread, not one ([338837d](https://github.com/NotRexed/Hookshot/commit/338837d541a048fc289bcdbbce9b9bb3f626facc))
* **vk:** keep four pages for the replay's reads as submitted, not one ([3bae557](https://github.com/NotRexed/Hookshot/commit/3bae55730d1316cd759df8c986b4615b183fdaf9))
* **vk:** keep four pages for the replay's reads as submitted, not one (Kokiri village view 50 -&gt; 57 fps) ([72133cb](https://github.com/NotRexed/Hookshot/commit/72133cb47db7ad6d66672600ab32ac2dbff26528))
* **vk:** keep the write-back shadows while VRAM is full of free space inside VMA's blocks ([6a4f3ea](https://github.com/NotRexed/Hookshot/commit/6a4f3eabb2c060e1fc6ef020f0b3576bcba5c0a0))
* **vk:** less copying of captured memory on the game and replay threads ([e63d30d](https://github.com/NotRexed/Hookshot/commit/e63d30dbea2bc8b660a890dd1620489d50e61229))
* **vk:** RX_VK_SUBMITTED_PAGES=1 keeps one submitted page, as before [#1762](https://github.com/NotRexed/Hookshot/issues/1762); tests follow [#1762](https://github.com/NotRexed/Hookshot/issues/1762) and [#1619](https://github.com/NotRexed/Hookshot/issues/1619) ([cfefd0e](https://github.com/NotRexed/Hookshot/commit/cfefd0e52445e33dfc49c1908209a2a9fdfa249e))
* **vk:** skip write-backs of large GPU-written bindings ([2ea6386](https://github.com/NotRexed/Hookshot/commit/2ea638622dbfd6d64192001a870dc339eac70eee))
* **vk:** skip write-backs of large GPU-written bindings ([30911ca](https://github.com/NotRexed/Hookshot/commit/30911cae063173878290b9ba660d1e97901a3cc8))
* **vk:** the capture's copies split by one function; a caller runs its own parts no helper has taken ([c747a2c](https://github.com/NotRexed/Hookshot/commit/c747a2c0c0984ce404bd9b761bf137d139e6f41e))
* **vk:** the capture's streaming-store copy is opt-in (RX_VK_CAPTURE_NT=1): more stutters, no lower capture time here ([#1841](https://github.com/NotRexed/Hookshot/issues/1841)) ([f4e6fdd](https://github.com/NotRexed/Hookshot/commit/f4e6fddec3c42c888dcee1e849747521d1e14f03))
* **vk:** the texture shadow's pages kept in place and copied by the capture helpers ([a4ccf07](https://github.com/NotRexed/Hookshot/commit/a4ccf073c830d7955361dd7b6ab44fe016b63e21))
* **vk:** the write-back shadows stay while VRAM is full of free space inside VMA's blocks ([23738b6](https://github.com/NotRexed/Hookshot/commit/23738b62378657d2a7060bed04580540dd259b53))
* **vk:** what play makes goes into a pipeline cache of its own, one file per save (no 0.5-1.2 GB rewrite in play) ([74c15be](https://github.com/NotRexed/Hookshot/commit/74c15be592b7cfc8399fcd05d592dcf6cc60c52a))
* **vk:** what play makes goes into a pipeline cache of its own, one per save, not the whole cache again ([1b0ea22](https://github.com/NotRexed/Hookshot/commit/1b0ea2244c4dd87ace939d9664ea2dba3a551eb9))
* **vk:** write the pipeline cache only when its data changed (no 1.1 GB rewrite at every start) ([bd5086a](https://github.com/NotRexed/Hookshot/commit/bd5086aa0a1674aa54323e6b9796252f0def6523))
* **vk:** write the pipeline cache only when its data changed since it was read or last written ([5a3fd42](https://github.com/NotRexed/Hookshot/commit/5a3fd42bf7749f807acecc3a4905482be405caf3))
* **vk:** write the pipeline cache only when its data changed since it was read or last written ([9f4fb8e](https://github.com/NotRexed/Hookshot/commit/9f4fb8e6267d42f51273e6a28c67b9a6dba086c8))

## [2.0.14](https://github.com/NotRexed/Hookshot/releases/tag/v2.0.14) (2026-11-10)

Entries from here on are written by release-please (docs/versioning.md). Releases up to v2.0.14 are described on their
[release pages](https://github.com/NotRexed/Hookshot/releases), and the first one also in `changelog.txt`.
