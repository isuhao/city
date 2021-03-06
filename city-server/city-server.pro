# параметры сборки, специфичные для окружения
win32:BOOST_INCLUDES_PATH = E:\\boost_1_54_0
win32:BOOST_LIBS_PATH = E:\\boost_1_54_0\\stage\\lib

# общие настройки
CONFIG += console
CONFIG += warn_on
CONFIG -= qt

# файлы проекта
HEADERS += \
	source/Connection.h \
	source/Position.h \
	source/Player.h \
	source/Level.h \
	source/Castle.h \
    source/Skeleton.h
SOURCES += \
	source/Connection.cpp \
	source/main.cpp \
	source/Position.cpp \
	source/Player.cpp \
	source/Level.cpp \
	source/Castle.cpp \
    source/Skeleton.cpp

# внешние библиотеки
win32 {
	INCLUDEPATH += $$BOOST_INCLUDES_PATH
	LIBS += -L$$BOOST_LIBS_PATH
	LIBS += \
		-lboost_system-mgw48-mt-1_54 \
		-lboost_program_options-mgw48-mt-1_54 \
		-lboost_regex-mgw48-mt-1_54 \
		-lboost_thread-mgw48-mt-1_54 \
		-lws2_32
}
unix:LIBS += \
	-lboost_system \
	-lboost_program_options \
	-lboost_regex \
	-lboost_thread

# флаги компилятора
QMAKE_CXXFLAGS += -std=c++03 -pedantic -Wall -W -O2
# отключение предупреждений в Boost
win32:QMAKE_CXXFLAGS += \
	-Wno-long-long \
	-Wno-unused-local-typedefs \
	-fno-strict-aliasing \
	-Wno-unused-variable
