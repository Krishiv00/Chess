SRC_DIR := src
INCLUDE_DIR := include
BUILD_DIR := build
OUT_DIR := dist

GUI_EXE := Chess++GUI.exe
UCI_EXE := Chess++UCI.exe

ICON_FILE := Resources/icon.ico
DIST_ASSETS := Resources

SFML_INCLUDE := C:/SFML/include
SFML_LIB := C:/SFML/lib

CXX := g++
STD := -std=c++23
MAKEFLAGS += -j

rwildcard = $(foreach d,$(wildcard $(1:=/*)),$(call rwildcard,$d,$2) $(filter $(subst *,%,$2),$d))

ENGINE_SRC := $(call rwildcard,$(SRC_DIR)/Engine,*.cpp)
GUI_SRC := $(call rwildcard,$(SRC_DIR)/GUI,*.cpp)
UCI_SRC := $(call rwildcard,$(SRC_DIR)/UCI,*.cpp)

GUI_DEV_OBJ := $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/dev/%.o,$(GUI_SRC)) \
               $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/dev/GUI/%.o,$(ENGINE_SRC))
GUI_REL_OBJ := $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/release/%.o,$(GUI_SRC)) \
               $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/release/GUI/%.o,$(ENGINE_SRC))
UCI_DEV_OBJ := $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/dev/%.o,$(UCI_SRC)) \
               $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/dev/UCI/%.o,$(ENGINE_SRC))
UCI_REL_OBJ := $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/release/%.o,$(UCI_SRC)) \
               $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/release/UCI/%.o,$(ENGINE_SRC))


UCI_INC := -I$(INCLUDE_DIR) -I$(INCLUDE_DIR)/Engine -I$(INCLUDE_DIR)/UCI
GUI_INC := -I$(INCLUDE_DIR) -I$(INCLUDE_DIR)/Engine -I$(INCLUDE_DIR)/GUI -I$(SFML_INCLUDE)

UCI_DEV_FLAGS := $(STD) $(UCI_INC) -fno-exceptions -Os -pipe -MMD -MP
UCI_REL_FLAGS := $(STD) $(UCI_INC) -fno-exceptions -O3 -DNDEBUG -ffunction-sections -fdata-sections

GUI_DEV_FLAGS := $(STD) $(GUI_INC) -fno-exceptions -Os -pipe -MMD -MP -DSFML_DYNAMIC
GUI_REL_FLAGS := $(STD) $(GUI_INC) -fno-exceptions -O3 -DNDEBUG -DSFML_STATIC -ffunction-sections -fdata-sections -fmerge-all-constants


GUI_DEV_LIBS := -L$(SFML_LIB) -lsfml-graphics -lsfml-window -lsfml-audio -lsfml-system

GUI_REL_LIBS := -L$(SFML_LIB) -lsfml-graphics-s -lsfml-window-s -lsfml-audio-s -lsfml-system-s \
                -lfreetype -lopengl32 -lgdi32 -lwinmm \
                -lflac -lvorbisenc -lvorbisfile -lvorbis -logg \
                -lpthread

ifneq ($(wildcard $(ICON_FILE)),)
RES_OBJ := $(BUILD_DIR)/resource.o
endif

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
	@if not exist "$(subst /,\,$(@D))" mkdir "$(subst /,\,$(@D))"
	@$(CXX) $(GUI_DEV_FLAGS) -c $< -o $@

$(BUILD_DIR)/release/GUI/%.o: $(SRC_DIR)/GUI/%.cpp FORCE
	@echo Compiling $<
	@if not exist "$(subst /,\,$(@D))" mkdir "$(subst /,\,$(@D))"
	@$(CXX) $(GUI_REL_FLAGS) -c $< -o $@

$(BUILD_DIR)/dev/UCI/%.o: $(SRC_DIR)/UCI/%.cpp Makefile
	@echo Compiling $<
	@if not exist "$(subst /,\,$(@D))" mkdir "$(subst /,\,$(@D))"
	@$(CXX) $(UCI_DEV_FLAGS) -c $< -o $@

$(BUILD_DIR)/release/UCI/%.o: $(SRC_DIR)/UCI/%.cpp FORCE
	@echo Compiling $<
	@if not exist "$(subst /,\,$(@D))" mkdir "$(subst /,\,$(@D))"
	@$(CXX) $(UCI_REL_FLAGS) -c $< -o $@

$(BUILD_DIR)/dev/GUI/Engine/%.o: $(SRC_DIR)/Engine/%.cpp Makefile
	@echo Compiling $<
	@if not exist "$(subst /,\,$(@D))" mkdir "$(subst /,\,$(@D))"
	@$(CXX) $(GUI_DEV_FLAGS) -c $< -o $@

$(BUILD_DIR)/release/GUI/Engine/%.o: $(SRC_DIR)/Engine/%.cpp FORCE
	@echo Compiling $<
	@if not exist "$(subst /,\,$(@D))" mkdir "$(subst /,\,$(@D))"
	@$(CXX) $(GUI_REL_FLAGS) -c $< -o $@

$(BUILD_DIR)/dev/UCI/Engine/%.o: $(SRC_DIR)/Engine/%.cpp Makefile
	@echo Compiling $<
	@if not exist "$(subst /,\,$(@D))" mkdir "$(subst /,\,$(@D))"
	@$(CXX) $(UCI_DEV_FLAGS) -c $< -o $@

$(BUILD_DIR)/release/UCI/Engine/%.o: $(SRC_DIR)/Engine/%.cpp FORCE
	@echo Compiling $<
	@if not exist "$(subst /,\,$(@D))" mkdir "$(subst /,\,$(@D))"
	@$(CXX) $(UCI_REL_FLAGS) -c $< -o $@

ifneq ($(wildcard $(ICON_FILE)),)
$(RES_OBJ): $(ICON_FILE)
	@echo Compiling $(ICON_FILE)
	@if not exist "$(BUILD_DIR)" mkdir "$(BUILD_DIR)"
	@echo IDI_ICON1 ICON "$(ICON_FILE)" > resource.rc
	@windres resource.rc -o $@
	@del /Q resource.rc
endif

copy-assets:
	@if not exist "$(OUT_DIR)" mkdir "$(OUT_DIR)"
	@if exist "$(DIST_ASSETS)\" (xcopy "$(DIST_ASSETS)" "$(OUT_DIR)\$(DIST_ASSETS)" /E /I /Y /Q >nul) else (copy /Y "$(DIST_ASSETS)" "$(OUT_DIR)" >nul)

FORCE:

clean:
	@echo Cleaning $(BUILD_DIR)/ and $(OUT_DIR)/
	@if exist "$(BUILD_DIR)" rmdir /S /Q "$(BUILD_DIR)"
	@if exist "$(OUT_DIR)" rmdir /S /Q "$(OUT_DIR)"
	@if exist "$(GUI_EXE)" del /Q "$(GUI_EXE)"
	@if exist "$(UCI_EXE)" del /Q "$(UCI_EXE)"

-include $(GUI_DEV_OBJ:.o=.d) $(UCI_DEV_OBJ:.o=.d)