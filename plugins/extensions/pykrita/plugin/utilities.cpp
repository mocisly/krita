// This file is part of PyKrita, Krita' Python scripting plugin.
//
// SPDX-FileCopyrightText: 2006 Paul Giannaros <paul@giannaros.org>
// SPDX-FileCopyrightText: 2012, 2013 Shaheed Haque <srhaque@theiet.org>
// SPDX-FileCopyrightText: 2021 L. E. Segovia <amy@amyspark.me>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
//

// config.h defines PYKRITA_PYTHON_LIBRARY, the path to libpython.so
// on the build system

#include "config.h"
#include "utilities.h"
#include "PythonPluginManager.h"

#include <algorithm>

#include <cmath>
#include <Python.h>

#include <QDir>
#include <QLibrary>
#include <QString>
#include <QStringList>
#include <QFileInfo>

#include <KoResourcePaths.h>
#include <kconfigbase.h>
#include <kconfiggroup.h>
#include <klocalizedstring.h>

#include <kis_debug.h>

#include "PykritaModule.h"

#define THREADED 1

namespace PyKrita
{
    static InitResult initStatus = INIT_UNINITIALIZED;
    static QScopedPointer<PythonPluginManager> pluginManagerInstance;

    InitResult initialize()
    {
        // Already initialized?
        if (initStatus == INIT_OK) return INIT_OK;

        dbgScript << "Initializing Python plugin for Python" << PY_MAJOR_VERSION << "," << PY_MINOR_VERSION;

        if (!Python::libraryLoad()) {
            return INIT_CANNOT_LOAD_PYTHON_LIBRARY;
        }

        // Update PYTHONPATH
        // 0) custom plugin directories (prefer local dir over systems')
        // 1) shipped krita module's dir
        QStringList pluginDirectories = KoResourcePaths::findDirs("pythonscripts");
        dbgScript << "Plugin Directories: " << pluginDirectories;
        if (!Python::setPath(pluginDirectories)) {
            initStatus = INIT_CANNOT_SET_PYTHON_PATHS;
            return initStatus;
        }

        if (0 != PyImport_AppendInittab(Python::PYKRITA_ENGINE, PYKRITA_INIT)) {
            initStatus = INIT_CANNOT_LOAD_PYKRITA_MODULE;
            return initStatus;
        }

        Python::ensureInitialized();
        Python py = Python();

        // Initialize 'plugins' dict of module 'pykrita'
        PyObject* plugins = PyDict_New();
        py.itemStringSet("plugins", plugins);

        pluginManagerInstance.reset(new PythonPluginManager());

        // Initialize our built-in module.
        auto pykritaModule = PYKRITA_INIT();

        if (!pykritaModule) {
            initStatus = INIT_CANNOT_LOAD_PYKRITA_MODULE;
            return initStatus;
            //return i18nc("@info:tooltip ", "No <icode>pykrita</icode> built-in module");
        }

        initStatus = INIT_OK;
        return initStatus;
    }

    PythonPluginManager *pluginManager()
    {
        auto pluginManager = pluginManagerInstance.data();
        KIS_SAFE_ASSERT_RECOVER_RETURN_VALUE(pluginManager, nullptr);
        return pluginManager;
    }

    void finalize() {
        dbgScript << "Going to destroy the Python engine";
        if (pluginManagerInstance) {
            pluginManagerInstance->unloadAllModules();

            PyKrita::Python::maybeFinalize();
            PyKrita::Python::libraryUnload();

            pluginManagerInstance.reset();
            initStatus = INIT_UNINITIALIZED;
        }
    }

    namespace
{
#ifndef Q_OS_WIN
QLibrary* s_pythonLibrary = 0;
#endif
PyThreadState* s_pythonThreadState = 0;
bool isPythonPathSet = false;
}                                                           // anonymous namespace

const char* Python::PYKRITA_ENGINE = "pykrita";

Python::Python()
{
#if THREADED
    m_state = PyGILState_Ensure();
#endif
}

Python::~Python()
{
#if THREADED
    PyGILState_Release(m_state);
#endif
}

bool Python::prependStringToList(PyObject* const list, const QString& value)
{
    PyObject* const u = unicode(value);
    bool result = !PyList_Insert(list, 0, u);
    Py_DECREF(u);
    if (!result)
        traceback(QString("Failed to prepend %1").arg(value));
    return result;
}

bool Python::functionCall(const char* const functionName, const char* const moduleName)
{
    PyObject* const result = functionCall(functionName, moduleName, PyTuple_New(0));
    if (result)
        Py_DECREF(result);
    return bool(result);
}

PyObject* Python::functionCall(
    const char* const functionName
    , const char* const moduleName
    , PyObject* const arguments
)
{
    if (!arguments) {
        errScript << "Missing arguments for" << moduleName << functionName;
        return 0;
    }
    PyObject* const func = itemString(functionName, moduleName);
    if (!func) {
        errScript << "Failed to resolve" << moduleName << functionName;
        return 0;
    }
    if (!PyCallable_Check(func)) {
        traceback(QString("Not callable %1.%2").arg(moduleName).arg(functionName));
        return 0;
    }
    PyObject* const result = PyObject_CallObject(func, arguments);
    Py_DECREF(arguments);
    if (!result)
        traceback(QString("No result from %1.%2").arg(moduleName).arg(functionName));

    return result;
}

bool Python::itemStringDel(const char* const item, const char* const moduleName)
{
    PyObject* const dict = moduleDict(moduleName);
    const bool result = dict && PyDict_DelItemString(dict, item);
    if (!result)
        traceback(QString("Could not delete item string %1.%2").arg(moduleName).arg(item));
    return result;
}

PyObject* Python::itemString(const char* const item, const char* const moduleName)
{
    if (PyObject* const value = itemString(item, moduleDict(moduleName)))
        return value;

    errScript << "Could not get item string" << moduleName << item;
    return 0;
}

PyObject* Python::itemString(const char* item, PyObject* dict)
{
    if (dict)
        if (PyObject* const value = PyDict_GetItemString(dict, item))
            return value;
    traceback(QString("Could not get item string %1").arg(item));
    return 0;
}

bool Python::itemStringSet(const char* const item, PyObject* const value, const char* const moduleName)
{
    PyObject* const dict = moduleDict(moduleName);
    const bool result = dict && !PyDict_SetItemString(dict, item, value);
    if (!result)
        traceback(QString("Could not set item string %1.%2").arg(moduleName).arg(item));
    return result;
}

PyObject* Python::kritaHandler(const char* const moduleName, const char* const handler)
{
    if (PyObject* const module = moduleImport(moduleName))
        return functionCall(handler, "krita", Py_BuildValue("(O)", module));
    return 0;
}

QString Python::lastTraceback() const
{
    QString result;
    result.swap(m_traceback);
    return result;
}

bool Python::libraryLoad()
{
    // no-op on Windows
#if defined(Q_OS_UNIX) && !defined(Q_OS_MAC)
    if (!s_pythonLibrary) {

        QFileInfo fi(PYKRITA_PYTHON_LIBRARY);
        // get the filename of the configured Python library, without the .so suffix
        const QString libraryName = fi.completeBaseName();
        // 1.0 is the SONAME of the shared Python library
        s_pythonLibrary = new QLibrary(libraryName, "1.0");
        s_pythonLibrary->setLoadHints(QLibrary::ExportExternalSymbolsHint);
        if (!s_pythonLibrary->load()) {
            dbgScript << QString("Could not load %1 -- Reason: %2").arg(s_pythonLibrary->fileName()).arg(s_pythonLibrary->errorString());
            delete s_pythonLibrary;
            s_pythonLibrary = 0;
            return false;
        }
        dbgScript << QString("Loaded %1").arg(s_pythonLibrary->fileName());
    }
#endif
    return true;
}

namespace
{

QString findKritaPythonLibsPath(const QString &libdir)
{
    QString rootPath(KoResourcePaths::getApplicationRoot());

    QDir rootDir(rootPath);
    QDir frameworkDir(rootPath + "Frameworks/Python.framework/Versions/Current");

    QFileInfoList candidates =
        frameworkDir.entryInfoList(QStringList() << "lib", QDir::Dirs | QDir::NoDotAndDotDot) +
        rootDir.entryInfoList(QStringList() << "lib*", QDir::Dirs | QDir::NoDotAndDotDot) +
        rootDir.entryInfoList(QStringList() << "Frameworks", QDir::Dirs | QDir::NoDotAndDotDot) +
        rootDir.entryInfoList(QStringList() << "share", QDir::Dirs | QDir::NoDotAndDotDot);
    Q_FOREACH (const QFileInfo &entry, candidates) {
        QDir libDir(entry.absoluteFilePath());
        if (libDir.cd(libdir)) {
            return libDir.absolutePath();
        } else {
            // Handle cases like Linux where libs are placed in a sub-dir
            // with the ABI name
            Q_FOREACH (const QFileInfo &subEntry, libDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
                QDir subDir(subEntry.absoluteFilePath());
                if (subDir.cd(libdir)) {
                    return subDir.absolutePath();
                }
            }
        }
    }
    return QString();
}

} // namespace

bool Python::setPath(const QStringList& scriptPaths)
{

    KIS_SAFE_ASSERT_RECOVER_RETURN_VALUE(!Py_IsInitialized(), false);
    KIS_SAFE_ASSERT_RECOVER_RETURN_VALUE(!isPythonPathSet, false);

//    qDebug() << ">>>>>>>>>>>" << qgetenv("APPDIR")
//             << KoResourcePaths::getApplicationRoot()
//             << (!qgetenv("APPDIR").isNull() && KoResourcePaths::getApplicationRoot().contains(qgetenv("APPDIR")));


#if defined Q_OS_WIN
    bool runningInBundle = false;
#elif defined Q_OS_MAC
    bool runningInBundle = KoResourcePaths::getApplicationRoot().toLower().contains("krita.app");
#else
    bool runningInBundle = (!qgetenv("APPDIR").isNull() &&
                             KoResourcePaths::getApplicationRoot().contains(qgetenv("APPDIR")));
#endif
    dbgScript << "Python::setPath. Script paths:" << scriptPaths << runningInBundle;

#ifdef Q_OS_WIN
    constexpr char pathSeparator = ';';
#else
    constexpr char pathSeparator = ':';
#endif

    QString originalPath;
    // Start with the script paths
    QStringList paths(scriptPaths);

    // Append the Krita libraries path
    QString pythonLibsPath = findKritaPythonLibsPath("krita-python-libs");
    dbgScript << "pythonLibsPath (krita-python-libs)" << pythonLibsPath;
    if (pythonLibsPath.isEmpty()) {
        dbgScript << "Cannot find krita-python-libs";
        return false;
    }
    dbgScript << "Found krita-python-libs at" << pythonLibsPath;
    paths.append(pythonLibsPath);

#ifndef Q_OS_WIN
    // Append the sip libraries path
    pythonLibsPath = findKritaPythonLibsPath("sip");
    dbgScript << "pythonLibsPath (sip)" << pythonLibsPath;
    if (!pythonLibsPath.isEmpty()) {
        dbgScript << "Found sip at" << pythonLibsPath;
        paths.append(pythonLibsPath);
    }
#endif

#ifdef Q_OS_WIN
    // Find embeddable Python at <root>/python
    QDir pythonDir(KoResourcePaths::getApplicationRoot());
    if (pythonDir.cd("python")) {
        const QString pythonHome = pythonDir.absolutePath();
        dbgScript << "Found bundled Python at" << pythonHome;
        // The default paths for Windows embeddable Python is
        // ./python[0-9][0-9]+.zip;./
        // Ordinarily, Python would find its own bundle here.
        // But because we set sys.path manually afterwards,
        // the prefix gets zeroed out. See
        // https://docs.python.org/3/c-api/init.html#c.Py_SetPath
        paths.append(pythonDir.absoluteFilePath(QStringLiteral("python%1%2.zip")
                                                    .arg(PY_MAJOR_VERSION)
                                                    .arg(PY_MINOR_VERSION)));
        paths.append(pythonDir.absolutePath());
    } else {
        errScript << "Bundled Python not found, cannot set Python library paths";
        return false;
    }

    // Add stock Python libs folder at <root>/lib/site-packages
    QString distToolsPath = findKritaPythonLibsPath("site-packages");
    dbgScript << "distToolsPath (site-packages)" << distToolsPath;
    if (distToolsPath.isEmpty()) {
        dbgScript << "Cannot find site-packages";
        return false;
    }
    dbgScript << "Found site-packages at" << distToolsPath;
    paths.append(distToolsPath);
#else
    // If using a system Python install, respect the current PYTHONPATH
    if (runningInBundle) {
        // We're running from an appimage, so we need our local python
        QString p = QFileInfo(PYKRITA_PYTHON_LIBRARY).fileName();
#ifdef Q_OS_MAC
        QString p2 = p.remove("lib").remove("m.dy").remove(".dy");
#else
        QString p2 = p.remove("lib").remove("m.so").remove(".so");
#endif
        dbgScript << "\t" << p << p2;
        originalPath = findKritaPythonLibsPath(p);
#ifdef Q_OS_MAC
        // Are we running with a system Python library instead?
        if (originalPath.isEmpty()) {
            // Keep the original Python search path.
            originalPath = QString::fromWCharArray(Py_GetPath());
            QString d = QFileInfo(PYKRITA_PYTHON_LIBRARY).absolutePath();

            paths.append(d + "/" + p2 + "/site-packages");
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
            paths.append(d + "/" + p2 + "/site-packages/PyQt6");
#else
            paths.append(d + "/" + p2 + "/site-packages/PyQt5");
#endif
            
        }
        else {
#endif
            paths.append(originalPath + "/lib-dynload");
            paths.append(originalPath + "/site-packages");
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
            paths.append(originalPath + "/site-packages/PyQt6");
#else
            paths.append(originalPath + "/site-packages/PyQt5");
#endif
#ifdef Q_OS_MAC
        }
#endif
    }
    else {
        // Use the system path
        originalPath = QString::fromLocal8Bit(qgetenv("PYTHONPATH"));
    }
#endif

    QString joinedPaths = paths.join(pathSeparator);
    if (!originalPath.isEmpty()) {
        joinedPaths = joinedPaths + pathSeparator + originalPath;
    }
    dbgScript << "Setting python paths:" << joinedPaths;
    qputenv("PYTHONPATH", joinedPaths.toLocal8Bit());

    isPythonPathSet = true;
    return true;
}

void Python::ensureInitialized()
{
    if (Py_IsInitialized()) {
        warnScript << "Python interpreter is already initialized, not initializing again";
    } else {
        dbgScript << "Initializing Python interpreter";
        Py_InitializeEx(0);
        if (!Py_IsInitialized()) {
            errScript << "Could not initialize Python interpreter";
        }
#if THREADED
        s_pythonThreadState = PyGILState_GetThisThreadState();
        PyEval_ReleaseThread(s_pythonThreadState);
#endif
    }
}

void Python::maybeFinalize()
{
    if (!Py_IsInitialized()) {
        warnScript << "Python interpreter not initialized, no need to finalize";
    } else {
#if THREADED
        PyEval_AcquireThread(s_pythonThreadState);
#endif
        Py_Finalize();
    }
}

void Python::libraryUnload()
{
    // no-op on Windows
#ifndef Q_OS_WIN
    if (s_pythonLibrary) {
        // Shut the interpreter down if it has been started.
        if (s_pythonLibrary->isLoaded()) {
            s_pythonLibrary->unload();
        }
        delete s_pythonLibrary;
        s_pythonLibrary = 0;
    }
#endif
}

PyObject* Python::moduleActions(const char* moduleName)
{
    return kritaHandler(moduleName, "moduleGetActions");
}

PyObject* Python::moduleConfigPages(const char* const moduleName)
{
    return kritaHandler(moduleName, "moduleGetConfigPages");
}

QString Python::moduleHelp(const char* moduleName)
{
    QString r;
    PyObject* const result = kritaHandler(moduleName, "moduleGetHelp");
    if (result) {
        r = unicode(result);
        Py_DECREF(result);
    }
    return r;
}

PyObject* Python::moduleDict(const char* const moduleName)
{
    PyObject* const module = moduleImport(moduleName);
    if (module)
        if (PyObject* const dictionary = PyModule_GetDict(module))
            return dictionary;

    traceback(QString("Could not get dict %1").arg(moduleName));
    return 0;
}

PyObject* Python::moduleImport(const char* const moduleName)
{
    PyObject* const module = PyImport_ImportModule(moduleName);
    if (module)
        return module;

    traceback(QString("Could not import %1").arg(moduleName));
    return 0;
}

// Inspired by https://lists.gt.net/python/python/150924.
void Python::traceback(const QString& description)
{
    m_traceback.clear();
    if (!PyErr_Occurred())
        // Return an empty string on no error.
        // NOTE "Return a string?" really??
        return;

    PyObject* exc_typ;
    PyObject* exc_val;
    PyObject* exc_tb;
    PyErr_Fetch(&exc_typ, &exc_val, &exc_tb);
    PyErr_NormalizeException(&exc_typ, &exc_val, &exc_tb);

    // Include the traceback.
    if (exc_tb) {
        m_traceback = "Traceback (most recent call last):\n";
        PyObject* const arguments = PyTuple_New(1);
        PyTuple_SetItem(arguments, 0, exc_tb);
        PyObject* const result = functionCall("format_tb", "traceback", arguments);
        if (result) {
            for (int i = 0, j = PyList_Size(result); i < j; i++) {
                PyObject* const tt = PyList_GetItem(result, i);
                PyObject* const t = Py_BuildValue("(O)", tt);
                char* buffer;
                if (!PyArg_ParseTuple(t, "s", &buffer))
                    break;
                m_traceback += buffer;
            }
            Py_DECREF(result);
        }
        Py_DECREF(exc_tb);
    }

    // Include the exception type and value.
    if (exc_typ) {
        PyObject* const temp = PyObject_GetAttrString(exc_typ, "__name__");
        if (temp) {
            m_traceback += unicode(temp);
            m_traceback += ": ";
        }
        Py_DECREF(exc_typ);
    }

    if (exc_val) {
        PyObject* const temp = PyObject_Str(exc_val);
        if (temp) {
            m_traceback += unicode(temp);
            m_traceback += "\n";
        }
        Py_DECREF(exc_val);
    }
    m_traceback += description;

    QStringList l = m_traceback.split("\n");
    Q_FOREACH(const QString &s, l) {
        errScript << s;
    }
    /// \todo How about to show it somewhere else than "console output"?
}

PyObject* Python::unicode(const QString& string)
{
    return PyUnicode_FromKindAndData(PyUnicode_2BYTE_KIND,
                                     string.constData(),
                                     string.length());
}

QString Python::unicode(PyObject* const string)
{
    if (!PyUnicode_Check(string))
        return QString();

    const int unichars = PyUnicode_GetLength(string);
    if (0 != PyUnicode_READY(string))
        return QString();

    switch (PyUnicode_KIND(string)) {
    case PyUnicode_1BYTE_KIND:
        return QString::fromLatin1((const char*)PyUnicode_1BYTE_DATA(string), unichars);
    case PyUnicode_2BYTE_KIND:
        return QString::fromUtf16(PyUnicode_2BYTE_DATA(string), unichars);
    case PyUnicode_4BYTE_KIND:
        return QString::fromUcs4(PyUnicode_4BYTE_DATA(string), unichars);
    default:
        break;
    }
    return QString();
}

bool Python::isUnicode(PyObject* const string)
{
    return PyUnicode_Check(string);
}

bool Python::prependPythonPaths(const QString& path)
{
    PyObject* sys_path = itemString("path", "sys");
    return bool(sys_path) && prependPythonPaths(path, sys_path);
}

bool Python::prependPythonPaths(const QStringList& paths)
{
    PyObject* sys_path = itemString("path", "sys");
    if (!sys_path)
        return false;

    /// \todo Heh, boosts' range adaptors would be good here!
    QStringList reversed_paths;
    std::reverse_copy(
        paths.begin()
        , paths.end()
        , std::back_inserter(reversed_paths)
    );

    Q_FOREACH(const QString & path, reversed_paths)
    if (!prependPythonPaths(path, sys_path))
        return false;

    return true;
}

bool Python::prependPythonPaths(const QString& path, PyObject* sys_path)
{
    Q_ASSERT("Dir entry expected to be valid" && sys_path);
    return bool(prependStringToList(sys_path, path));
}

}                                                           // namespace PyKrita

// krita: indent-width 4;
