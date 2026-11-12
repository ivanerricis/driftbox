# Third-party notices

Driftbox itself is licensed under AGPL-3.0-or-later. It uses the components below. Versions are those pinned in `cmake/Dependencies.cmake`.

| Component | Version | Licence | Use |
|---|---|---|---|
| [JUCE](https://github.com/juce-framework/JUCE) | 8.0.15 | AGPL-3.0 (JUCE modules are dual-licensed AGPLv3 / commercial; Driftbox uses the AGPLv3 option) | Plugin framework |
| [VST3 SDK](https://github.com/steinbergmedia/vst3sdk) (bundled with JUCE) | as bundled with JUCE 8.0.15 | MIT (Copyright (c) 2025 Steinberg Media Technologies GmbH) | VST3 interfaces |
| [CLAP](https://github.com/free-audio/clap) | as submodule of clap-juce-extensions | MIT (Copyright (c) 2021 Alexandre BIQUE) | CLAP interfaces |
| [clap-juce-extensions](https://github.com/free-audio/clap-juce-extensions) | commit 7adee3a1bd4684d4caa5601100e364abccff4b4d | MIT (Copyright 2019-2020 Paul Walker) | CLAP wrapper for JUCE |
| [Catch2](https://github.com/catchorg/Catch2) | v3.16.1 | BSL-1.0 | Unit tests only, not shipped |
| [CPM.cmake](https://github.com/cpm-cmake/CPM.cmake) | v0.43.2 | MIT | Dependency fetching at build time, not shipped |
| [IBM Plex](https://github.com/IBM/plex) fonts | Plex Sans 3.005, Plex Mono 2.3 | SIL OFL-1.1 (`resources/ui/fonts/licenses/OFL-ibmplex.txt`) | UI typography, embedded in the plugin binary |
| [pluginval](https://github.com/Tracktion/pluginval) | CI only | GPL-3.0 | Plugin validation in CI, not linked or shipped |

Full licence texts are available in the respective upstream repositories and in the dependency sources fetched during the build. When binaries are distributed, the notices above must accompany them.

The VST3 SDK is included in JUCE under the MIT licence; the Steinberg VST3 trademark and logo usage guidelines apply to any use of the VST logo.
