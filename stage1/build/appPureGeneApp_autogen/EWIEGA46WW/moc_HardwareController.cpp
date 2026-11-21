/****************************************************************************
** Meta object code from reading C++ file 'HardwareController.h'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.8.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../HardwareController.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'HardwareController.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 68
#error "This file was generated using the moc from 6.8.2. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

#ifndef Q_CONSTINIT
#define Q_CONSTINIT
#endif

QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
QT_WARNING_DISABLE_GCC("-Wuseless-cast")
namespace {
struct qt_meta_tag_ZN18HardwareControllerE_t {};
} // unnamed namespace


#ifdef QT_MOC_HAS_STRINGDATA
static constexpr auto qt_meta_stringdata_ZN18HardwareControllerE = QtMocHelpers::stringData(
    "HardwareController",
    "tempsUpdated",
    "",
    "graphDataUpdated",
    "spot",
    "cycle",
    "rfu",
    "runComplete",
    "message",
    "trayIn",
    "trayOut",
    "startHeaters",
    "stopHeaters",
    "startOptical",
    "targetCycles",
    "stopOptical",
    "setTopTemp",
    "temp",
    "setBottomTemp",
    "topTemp",
    "bottomTemp"
);
#else  // !QT_MOC_HAS_STRINGDATA
#error "qtmochelpers.h not found or too old."
#endif // !QT_MOC_HAS_STRINGDATA

Q_CONSTINIT static const uint qt_meta_data_ZN18HardwareControllerE[] = {

 // content:
      12,       // revision
       0,       // classname
       0,    0, // classinfo
      11,   14, // methods
       2,  105, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       3,       // signalCount

 // signals: name, argc, parameters, tag, flags, initial metatype offsets
       1,    0,   80,    2, 0x06,    3 /* Public */,
       3,    3,   81,    2, 0x06,    4 /* Public */,
       7,    1,   88,    2, 0x06,    8 /* Public */,

 // slots: name, argc, parameters, tag, flags, initial metatype offsets
       9,    0,   91,    2, 0x0a,   10 /* Public */,
      10,    0,   92,    2, 0x0a,   11 /* Public */,
      11,    0,   93,    2, 0x0a,   12 /* Public */,
      12,    0,   94,    2, 0x0a,   13 /* Public */,
      13,    1,   95,    2, 0x0a,   14 /* Public */,
      15,    0,   98,    2, 0x0a,   16 /* Public */,
      16,    1,   99,    2, 0x0a,   17 /* Public */,
      18,    1,  102,    2, 0x0a,   19 /* Public */,

 // signals: parameters
    QMetaType::Void,
    QMetaType::Void, QMetaType::Int, QMetaType::Int, QMetaType::Float,    4,    5,    6,
    QMetaType::Void, QMetaType::QString,    8,

 // slots: parameters
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Int,   14,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Float,   17,
    QMetaType::Void, QMetaType::Float,   17,

 // properties: name, type, flags, notifyId, revision
      19, QMetaType::Float, 0x00015001, uint(0), 0,
      20, QMetaType::Float, 0x00015001, uint(0), 0,

       0        // eod
};

Q_CONSTINIT const QMetaObject HardwareController::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_meta_stringdata_ZN18HardwareControllerE.offsetsAndSizes,
    qt_meta_data_ZN18HardwareControllerE,
    qt_static_metacall,
    nullptr,
    qt_incomplete_metaTypeArray<qt_meta_tag_ZN18HardwareControllerE_t,
        // property 'topTemp'
        QtPrivate::TypeAndForceComplete<float, std::true_type>,
        // property 'bottomTemp'
        QtPrivate::TypeAndForceComplete<float, std::true_type>,
        // Q_OBJECT / Q_GADGET
        QtPrivate::TypeAndForceComplete<HardwareController, std::true_type>,
        // method 'tempsUpdated'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'graphDataUpdated'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        QtPrivate::TypeAndForceComplete<float, std::false_type>,
        // method 'runComplete'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<QString, std::false_type>,
        // method 'trayIn'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'trayOut'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'startHeaters'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'stopHeaters'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'startOptical'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        // method 'stopOptical'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'setTopTemp'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<float, std::false_type>,
        // method 'setBottomTemp'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<float, std::false_type>
    >,
    nullptr
} };

void HardwareController::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<HardwareController *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->tempsUpdated(); break;
        case 1: _t->graphDataUpdated((*reinterpret_cast< std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<float>>(_a[3]))); break;
        case 2: _t->runComplete((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 3: _t->trayIn(); break;
        case 4: _t->trayOut(); break;
        case 5: _t->startHeaters(); break;
        case 6: _t->stopHeaters(); break;
        case 7: _t->startOptical((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 8: _t->stopOptical(); break;
        case 9: _t->setTopTemp((*reinterpret_cast< std::add_pointer_t<float>>(_a[1]))); break;
        case 10: _t->setBottomTemp((*reinterpret_cast< std::add_pointer_t<float>>(_a[1]))); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _q_method_type = void (HardwareController::*)();
            if (_q_method_type _q_method = &HardwareController::tempsUpdated; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 0;
                return;
            }
        }
        {
            using _q_method_type = void (HardwareController::*)(int , int , float );
            if (_q_method_type _q_method = &HardwareController::graphDataUpdated; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 1;
                return;
            }
        }
        {
            using _q_method_type = void (HardwareController::*)(QString );
            if (_q_method_type _q_method = &HardwareController::runComplete; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 2;
                return;
            }
        }
    }
    if (_c == QMetaObject::ReadProperty) {
        void *_v = _a[0];
        switch (_id) {
        case 0: *reinterpret_cast< float*>(_v) = _t->topTemp(); break;
        case 1: *reinterpret_cast< float*>(_v) = _t->bottomTemp(); break;
        default: break;
        }
    }
}

const QMetaObject *HardwareController::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *HardwareController::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_ZN18HardwareControllerE.stringdata0))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int HardwareController::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 11)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 11;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 11)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 11;
    }
    if (_c == QMetaObject::ReadProperty || _c == QMetaObject::WriteProperty
            || _c == QMetaObject::ResetProperty || _c == QMetaObject::BindableProperty
            || _c == QMetaObject::RegisterPropertyMetaType) {
        qt_static_metacall(this, _c, _id, _a);
        _id -= 2;
    }
    return _id;
}

// SIGNAL 0
void HardwareController::tempsUpdated()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void HardwareController::graphDataUpdated(int _t1, int _t2, float _t3)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t3))) };
    QMetaObject::activate(this, &staticMetaObject, 1, _a);
}

// SIGNAL 2
void HardwareController::runComplete(QString _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 2, _a);
}
QT_WARNING_POP
