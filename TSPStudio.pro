QT += core gui qml quick quickcontrols2

CONFIG += c++17 strict_c++

TARGET = TSPStudio
TEMPLATE = app

INCLUDEPATH += include \
               include/core/cdt

SOURCES += \
    src/main.cpp \
    src/ui/GuiController.cpp \
    src/ui/TSPStudioMapView.cpp \
    src/core/TSPStudioManager.cpp \
    src/solvers/SolverWorker.cpp

HEADERS += \
    include/ui/GuiController.h \
    include/ui/TSPStudioMapView.h \
    include/core/City.h \
    include/core/TSPStudioManager.h \
    include/solvers/SolverWorker.h

RESOURCES += \
    resources.qrc

win32 {
    LIBS += -luser32
    DEFINES += _CRT_SECURE_NO_WARNINGS
}
