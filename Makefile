SRC_DIR := src
INCLUDE_DIR := include
BUILD_DIR := build
OUT_DIR := dist

GUI_EXE := Chess++GUI.exe
UCI_EXE := Chess++UCI.exe

ICON_PNG := Resources/icon.png
ICON_PREBUILT := Resources/icon.ico
ICON_FILE := icon.ico
DIST_ASSETS := Resources

SFML_INCLUDE := C:/SFML/include
SFML_LIB := C:/SFML/lib

CXX := g++
STD := -std=c++23
MAKEFLAGS += -j8

rwildcard = $(foreach d,$(wildcard $(1:=/*)),$(call rwildcard,$d,$2) $(filter $(subst *,%,$2),$d))

ENGINE_SRC := $(call rwildcard,$(SRC_DIR)/Engine,*.cpp)
GUI_SRC := $(call rwildcard,$(SRC_DIR)/GUI,*.cpp)
UCI_SRC := $(call rwildcard,$(SRC_DIR)/UCI,*.cpp)

ENGINE_DEV_OBJ := $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/dev/%.o,$(ENGINE_SRC))
ENGINE_REL_OBJ := $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/release/%.o,$(ENGINE_SRC))

GUI_DEV_OBJ := $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/dev/%.o,$(GUI_SRC)) $(ENGINE_DEV_OBJ)
GUI_REL_OBJ := $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/release/%.o,$(GUI_SRC)) $(ENGINE_REL_OBJ)
UCI_DEV_OBJ := $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/dev/%.o,$(UCI_SRC)) $(ENGINE_DEV_OBJ)
UCI_REL_OBJ := $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/release/%.o,$(UCI_SRC)) $(ENGINE_REL_OBJ)

UCI_INC := -I$(INCLUDE_DIR) -I$(INCLUDE_DIR)/Engine -I$(INCLUDE_DIR)/UCI
GUI_INC := -I$(INCLUDE_DIR) -I$(INCLUDE_DIR)/Engine -I$(INCLUDE_DIR)/GUI -I$(SFML_INCLUDE)
ENGINE_INC := -I$(INCLUDE_DIR) -I$(INCLUDE_DIR)/Engine

UCI_DEV_FLAGS := $(STD) $(UCI_INC) -fno-exceptions -Os -pipe -MMD -MP
UCI_REL_FLAGS := $(STD) $(UCI_INC) -fno-exceptions -O3 -DNDEBUG -ffunction-sections -fdata-sections

GUI_DEV_FLAGS := $(STD) $(GUI_INC) -fno-exceptions -Os -pipe -MMD -MP -DSFML_DYNAMIC
GUI_REL_FLAGS := $(STD) $(GUI_INC) -fno-exceptions -O3 -DNDEBUG -DSFML_STATIC -ffunction-sections -fdata-sections -fmerge-all-constants

ENGINE_DEV_FLAGS := $(STD) $(ENGINE_INC) -fno-exceptions -Os -pipe -MMD -MP
ENGINE_REL_FLAGS := $(STD) $(ENGINE_INC) -fno-exceptions -O3 -DNDEBUG -ffunction-sections -fdata-sections

GUI_DEV_LIBS := -L$(SFML_LIB) -lsfml-graphics -lsfml-window -lsfml-audio -lsfml-network -lsfml-system
GUI_REL_LIBS := -L$(SFML_LIB) -lsfml-graphics-s -lsfml-window-s -lsfml-audio-s -lsfml-network-s -lsfml-system-s -lfreetype -lharfbuzz -lmbedtls -lmbedx509 -lmbedcrypto -lopengl32 -lgdi32 -lwinmm -lws2_32 -ldnsapi -lflac -lvorbisenc -lvorbisfile -lvorbis -logg -lpthread -lbcrypt -lcrypt32

RES_OBJ := $(BUILD_DIR)/resource.o

.PHONY: all dev release gui uci release-gui release-uci clean FORCE copy-assets

all: dev
dev: gui uci
release: release-gui release-uci

gui: $(GUI_EXE)
release-gui: $(OUT_DIR)/$(GUI_EXE)

$(GUI_EXE): $(GUI_DEV_OBJ) $(RES_OBJ)
	@echo Linking $@
	@$(CXX) $(GUI_DEV_OBJ) $(RES_OBJ) -o $@ $(GUI_DEV_LIBS)

$(OUT_DIR)/$(GUI_EXE): $(GUI_REL_OBJ) $(RES_OBJ) copy-assets
	@echo Linking $@
	@$(CXX) -static -s -mwindows -Wl,--gc-sections -L$(SFML_LIB) $(GUI_REL_OBJ) $(RES_OBJ) -o $@ $(GUI_REL_LIBS)

uci: $(UCI_EXE)
release-uci: $(OUT_DIR)/$(UCI_EXE)

$(UCI_EXE): $(UCI_DEV_OBJ)
	@echo Linking $@
	@$(CXX) $(UCI_DEV_OBJ) -o $@

$(OUT_DIR)/$(UCI_EXE): $(UCI_REL_OBJ) copy-assets
	@echo Linking $@
	@$(CXX) -static -s -Wl,--gc-sections $(UCI_REL_OBJ) -o $@

$(BUILD_DIR)/dev/GUI/%.o: $(SRC_DIR)/GUI/%.cpp Makefile
	@echo Compiling $<
	@if not exist "$(subst /,\,$(@D))" mkdir "$(subst /,\,$(@D))" 2>nul & exit 0
	@$(CXX) $(GUI_DEV_FLAGS) -c $< -o $@

$(BUILD_DIR)/release/GUI/%.o: $(SRC_DIR)/GUI/%.cpp FORCE
	@echo Compiling $<
	@if not exist "$(subst /,\,$(@D))" mkdir "$(subst /,\,$(@D))" 2>nul & exit 0
	@$(CXX) $(GUI_REL_FLAGS) -c $< -o $@

$(BUILD_DIR)/dev/UCI/%.o: $(SRC_DIR)/UCI/%.cpp Makefile
	@echo Compiling $<
	@if not exist "$(subst /,\,$(@D))" mkdir "$(subst /,\,$(@D))" 2>nul & exit 0
	@$(CXX) $(UCI_DEV_FLAGS) -c $< -o $@

$(BUILD_DIR)/release/UCI/%.o: $(SRC_DIR)/UCI/%.cpp FORCE
	@echo Compiling $<
	@if not exist "$(subst /,\,$(@D))" mkdir "$(subst /,\,$(@D))" 2>nul & exit 0
	@$(CXX) $(UCI_REL_FLAGS) -c $< -o $@

$(BUILD_DIR)/dev/Engine/%.o: $(SRC_DIR)/Engine/%.cpp Makefile
	@echo Compiling $<
	@if not exist "$(subst /,\,$(@D))" mkdir "$(subst /,\,$(@D))" 2>nul & exit 0
	@$(CXX) $(ENGINE_DEV_FLAGS) -c $< -o $@

$(BUILD_DIR)/release/Engine/%.o: $(SRC_DIR)/Engine/%.cpp FORCE
	@echo Compiling $<
	@if not exist "$(subst /,\,$(@D))" mkdir "$(subst /,\,$(@D))" 2>nul & exit 0
	@$(CXX) $(ENGINE_REL_FLAGS) -c $< -o $@

$(RES_OBJ): $(ICON_PNG)
ifneq ($(wildcard $(ICON_PREBUILT)),)
	@if not exist "$(BUILD_DIR)" mkdir "$(BUILD_DIR)" 2>nul & exit 0
	@echo IDI_ICON1 ICON "$(ICON_PREBUILT)" > resource.rc
	@windres resource.rc -o $@
	@del /Q resource.rc
else
	@if not exist "$(BUILD_DIR)" mkdir "$(BUILD_DIR)" 2>nul & exit 0
	@ffmpeg -y -loglevel error -i "$(ICON_PNG)" -vf scale=256:256:flags=lanczos -frames:v 1 -c:v png "$(ICON_FILE)"
	@echo IDI_ICON1 ICON "$(ICON_FILE)" > resource.rc
	@windres resource.rc -o $@
	@del /Q resource.rc
	@del /Q "$(ICON_FILE)"
endif

copy-assets:
	@if not exist "$(OUT_DIR)" mkdir "$(OUT_DIR)"
	@if exist "$(DIST_ASSETS)\" (echo icon.ico> xcopy_exclude.txt && xcopy "$(DIST_ASSETS)" "$(OUT_DIR)\$(DIST_ASSETS)" /E /I /Y /Q /EXCLUDE:xcopy_exclude.txt >nul && del /Q xcopy_exclude.txt) else (copy /Y "$(DIST_ASSETS)" "$(OUT_DIR)" >nul)

FORCE:

clean:
	@echo Cleaning Up!
	@if exist "$(BUILD_DIR)" rmdir /S /Q "$(BUILD_DIR)"
	@if exist "$(OUT_DIR)" rmdir /S /Q "$(OUT_DIR)"
	@if exist "$(ICON_FILE)" del /Q "$(ICON_FILE)"
	@if exist resource.rc del /Q resource.rc
	@if exist xcopy_exclude.txt del /Q xcopy_exclude.txt

-include $(GUI_DEV_OBJ:.o=.d) $(UCI_DEV_OBJ:.o=.d) $(GUI_REL_OBJ:.o=.d) $(UCI_REL_OBJ:.o=.d)
