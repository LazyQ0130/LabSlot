# Windows 使用 MinGW-w64 的 mingw32-make，或其他 GNU make。
CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -Werror
CPPFLAGS = -Iinclude
SOURCES = $(wildcard src/*.c)
HEADERS = $(wildcard include/*.h)
TARGET = smart-lab.exe
PYTHON = python
WINDRES = windres
GUI_TARGET = LabSlot-GUI.exe
GUI_SOURCES = $(filter-out src/main.c src/ui.c,$(SOURCES)) $(wildcard src/gui/*.c)
GUI_RESOURCE = src/gui/gui_resource.o
GUI_LIBS = -lcomctl32 -lgdi32 -luser32

.PHONY: all gui test test-p0 test-p1 test-p2 test-p3 test-p4 test-gui test-gui-student test-gui-admin test-gui-final analyze
all: $(TARGET) $(GUI_TARGET)
gui: $(GUI_TARGET)

$(GUI_RESOURCE): src/gui/gui.rc src/gui/gui.manifest
	$(WINDRES) -I src/gui src/gui/gui.rc -o $@

$(GUI_TARGET): $(GUI_SOURCES) $(HEADERS) $(GUI_RESOURCE)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(GUI_SOURCES) $(GUI_RESOURCE) -mwindows $(GUI_LIBS) -o $@

$(TARGET): $(SOURCES) $(HEADERS)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(SOURCES) -o $@

test: test-p0 test-p1 test-p2 test-p3 test-p4 test-p6-1

test-p6-1: $(TARGET) $(GUI_TARGET)
	$(PYTHON) tests/test_p6_1.py --cc "$(CC)"

test-p0: $(TARGET)
	$(PYTHON) tests/test_p0.py --executable $(TARGET) --cc "$(CC)"

test-p1: $(TARGET)
	$(PYTHON) tests/test_p1.py --executable $(TARGET) --cc "$(CC)"

test-p2: $(TARGET)
	$(PYTHON) tests/test_p2.py --executable $(TARGET) --cc "$(CC)"

test-p3: $(TARGET)
	$(PYTHON) tests/test_p3.py --executable $(TARGET) --cc "$(CC)"

test-p4: $(TARGET)
	$(PYTHON) tests/test_p4.py --executable $(TARGET) --cc "$(CC)"

test-gui: $(GUI_TARGET)
	$(PYTHON) tests/test_gui.py --cc "$(CC)"
	$(PYTHON) tests/test_gui_student.py --cc "$(CC)"
	$(PYTHON) tests/test_gui_admin.py --cc "$(CC)"
	$(PYTHON) tests/test_gui_final.py --cc "$(CC)"

test-gui-student: $(GUI_TARGET)
	$(PYTHON) tests/test_gui_student.py --cc "$(CC)"

test-gui-admin: $(GUI_TARGET)
	$(PYTHON) tests/test_gui_admin.py --cc "$(CC)"

test-gui-final: $(GUI_TARGET)
	$(PYTHON) tests/test_gui_final.py --cc "$(CC)"

analyze:
	$(PYTHON) scripts/analyze.py --cc "$(CC)"

# 不提供批量删除的 clean 目标；测试输出与运行数据由使用者按需保留。
