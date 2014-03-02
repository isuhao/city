# общие настройки
CONFIG += console
CONFIG += warn_on
CONFIG -= qt

# файлы проекта
SOURCES += source/main.cpp

# внешние библиотеки
LIBS += -lboost_system -lboost_program_options -lboost_regex -lpthread

# флаги компилятора
QMAKE_CXXFLAGS += -std=c++03 -pedantic -Wall -W -O2
