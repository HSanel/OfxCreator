#include "KernelRuntime.h"
#include "PortInference.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QUuid>

#include <algorithm>

namespace {

bool isWindowsStoreStub(QString const &path)
{
  QString const normalized = QDir::fromNativeSeparators(path).toLower();
  return normalized.contains(QLatin1String("/windowsapps/"));
}

bool probePython(QString const &program, QStringList const &args)
{
  if (program.isEmpty() || isWindowsStoreStub(program)) {
    return false;
  }
  QProcess proc;
  proc.setProcessChannelMode(QProcess::MergedChannels);
  QStringList probe = args;
  probe << QStringLiteral("-c") << QStringLiteral("import sys; print(sys.version_info[0])");
  proc.start(program, probe);
  if (!proc.waitForStarted(4000)) {
    return false;
  }
  if (!proc.waitForFinished(8000)) {
    proc.kill();
    return false;
  }
  QByteArray const out = proc.readAll();
  if (out.contains("Microsoft Store") || out.contains("App execution aliases")) {
    return false;
  }
  return proc.exitCode() == 0 && out.contains('3');
}

struct PythonCommand
{
  QString program;
  QStringList prefixArgs;
};

PythonCommand pythonCommand()
{
  static PythonCommand cached;
  if (!cached.program.isEmpty()) {
    return cached;
  }

  QList<PythonCommand> candidates;
  QString const fromEnv = qEnvironmentVariable("NR_PYTHON");
  if (!fromEnv.isEmpty()) {
    candidates.push_back({fromEnv, {}});
  }

  QStringList extraDirs;
  extraDirs << QCoreApplication::applicationDirPath();
  extraDirs << QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("python"));
  extraDirs << QStringLiteral("C:/Python314") << QStringLiteral("C:/Python313") << QStringLiteral("C:/Python312")
            << QStringLiteral("C:/Python311") << QStringLiteral("C:/Program Files/Python312")
            << QStringLiteral("C:/Program Files/Python313");

  QDir localPython(QDir(qEnvironmentVariable("LOCALAPPDATA")).filePath(QStringLiteral("Programs/Python")));
  if (localPython.exists()) {
    QStringList const versions = localPython.entryList(QStringList{QStringLiteral("Python3*")}, QDir::Dirs);
    for (QString const &version : versions) {
      extraDirs << localPython.filePath(version);
    }
  }

  for (QString const &dir : extraDirs) {
    QString const exe = QDir(dir).filePath(QStringLiteral("python.exe"));
    if (QFileInfo::exists(exe) && !isWindowsStoreStub(exe)) {
      candidates.push_back({exe, {}});
    }
  }

  for (QString const &name : {QStringLiteral("py"), QStringLiteral("python"), QStringLiteral("python3")}) {
    QString const exe = QStandardPaths::findExecutable(name);
    if (exe.isEmpty()) {
      continue;
    }
    if (name == QLatin1String("py")) {
      candidates.push_back({exe, {QStringLiteral("-3")}});
    } else {
      candidates.push_back({exe, {}});
    }
  }

  for (PythonCommand const &candidate : candidates) {
    if (probePython(candidate.program, candidate.prefixArgs)) {
      cached = candidate;
      return cached;
    }
  }
  return {};
}

QString missingPythonMessage()
{
  return QStringLiteral(
    "Kein Python 3 gefunden. Der Windows-Store-Alias wird ignoriert. "
    "Python von python.org installieren oder NR_PYTHON auf python.exe setzen.");
}

QString scriptDir()
{
  QString const root = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
  QDir dir(root.isEmpty() ? QDir::tempPath() : root);
  dir.mkpath(QStringLiteral("ofx-host/python-kernels"));
  return dir.filePath(QStringLiteral("ofx-host/python-kernels"));
}

class PythonRuntime final : public KernelRuntime
{
public:
  bool load(QString const &source, QString *error) override
  {
    m_source = source;
    if (m_source.trimmed().isEmpty()) {
      if (error) {
        *error = QStringLiteral("Python-Quelle ist leer.");
      }
      return false;
    }
    if (!m_source.contains(QLatin1String("def process"))) {
      if (error) {
        *error = QStringLiteral("Python-Kernel braucht def process(width, height, rgba).");
      }
      return false;
    }
    if (pythonCommand().program.isEmpty()) {
      if (error) {
        *error = missingPythonMessage();
      }
      return false;
    }
    m_layout = inferKernelPorts(NodeKind::Python, source);
    return true;
  }

  bool process(QVector<RgbaImage> const &inputs, QVector<RgbaImage> *outputs, QString *error) override
  {
    if (m_source.isEmpty() || !outputs || inputs.isEmpty()) {
      if (error) {
        *error = QStringLiteral("Python-Kernel ist nicht geladen.");
      }
      return false;
    }

    int const nIn = std::min(m_layout.inCount, int(inputs.size()));
    int const nOut = m_layout.outCount;
    QString const path = QDir(scriptDir()).filePath(QUuid::createUuid().toString(QUuid::Id128) + QStringLiteral(".py"));
    QByteArray script;
    script += m_source.toUtf8();
    script += "\n\nimport struct, sys\n";
    script += "def _nr_run():\n";
    script += "    buf = sys.stdin.buffer\n";
    script += "    header = buf.read(16)\n";
    script += "    if len(header) != 16:\n";
    script += "        raise RuntimeError('stdin header')\n";
    script += "    width, height, n_in, n_out = struct.unpack('<IIII', header)\n";
    script += "    n = width * height * 4\n";
    script += "    images = []\n";
    script += "    for _ in range(n_in):\n";
    script += "        rgba = bytearray(buf.read(n))\n";
    script += "        if len(rgba) != n:\n";
    script += "            raise RuntimeError('stdin pixels')\n";
    script += "        images.append(rgba)\n";
    script += "    result = process(width, height, *images)\n";
    script += "    if isinstance(result, (bytes, bytearray)):\n";
    script += "        result = [result]\n";
    script += "    if not isinstance(result, (list, tuple)) or len(result) != n_out:\n";
    script += "        raise TypeError('process() muss n_out Byte-Buffer zurückgeben')\n";
    script += "    sys.stdout.buffer.write(struct.pack('<II', width, height))\n";
    script += "    for item in result:\n";
    script += "        if not isinstance(item, (bytes, bytearray)) or len(item) != n:\n";
    script += "            raise ValueError('process() Größe stimmt nicht')\n";
    script += "        sys.stdout.buffer.write(item)\n";
    script += "_nr_run()\n";

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
      if (error) {
        *error = QStringLiteral("Python-Skript konnte nicht geschrieben werden.");
      }
      return false;
    }
    file.write(script);
    file.close();

    PythonCommand const python = pythonCommand();
    if (python.program.isEmpty()) {
      if (error) {
        *error = missingPythonMessage();
      }
      QFile::remove(path);
      return false;
    }
    QStringList extra = python.prefixArgs;
    extra << path;

    QProcess proc;
    proc.setProcessChannelMode(QProcess::SeparateChannels);
    proc.start(python.program, extra);
    if (!proc.waitForStarted(5000)) {
      if (error) {
        *error = missingPythonMessage();
      }
      QFile::remove(path);
      return false;
    }

    auto appendU32 = [](QByteArray &bytes, quint32 value) {
      bytes.append(char(value & 0xff));
      bytes.append(char((value >> 8) & 0xff));
      bytes.append(char((value >> 16) & 0xff));
      bytes.append(char((value >> 24) & 0xff));
    };
    QByteArray payload;
    appendU32(payload, quint32(inputs[0].width));
    appendU32(payload, quint32(inputs[0].height));
    appendU32(payload, quint32(nIn));
    appendU32(payload, quint32(nOut));
    for (int i = 0; i < nIn; ++i) {
      payload += inputs[i].pixels;
    }
    proc.write(payload);
    proc.closeWriteChannel();
    if (!proc.waitForFinished(120000)) {
      proc.kill();
      if (error) {
        *error = QStringLiteral("Python-Kernel Timeout.");
      }
      QFile::remove(path);
      return false;
    }
    QFile::remove(path);

    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
      if (error) {
        *error = QString::fromUtf8(proc.readAllStandardError()).trimmed();
        if (error->isEmpty()) {
          *error = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
        }
        if (error->isEmpty()) {
          *error = QStringLiteral("Python-Kernel fehlgeschlagen (Exit %1).").arg(proc.exitCode());
        }
      }
      return false;
    }

    QByteArray stdoutData = proc.readAllStandardOutput();
    int const needed = 8 + nOut * inputs[0].byteCount();
    if (stdoutData.size() < needed) {
      if (error) {
        *error = QStringLiteral("Python-Kernel lieferte zu wenig Daten.");
      }
      return false;
    }
    outputs->resize(nOut);
    int offset = 8;
    for (int i = 0; i < nOut; ++i) {
      (*outputs)[i].width = inputs[0].width;
      (*outputs)[i].height = inputs[0].height;
      (*outputs)[i].pixels = stdoutData.mid(offset, inputs[0].byteCount());
      offset += inputs[0].byteCount();
    }
    return true;
  }

private:
  QString m_source;
  KernelPortLayout m_layout;
};

} // namespace

std::unique_ptr<KernelRuntime> createPythonRuntime()
{
  return std::make_unique<PythonRuntime>();
}
