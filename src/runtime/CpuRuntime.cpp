#include "KernelRuntime.h"
#include "PortInference.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLibrary>
#include <QProcess>
#include <QStandardPaths>
#include <QUuid>

#include <algorithm>

#include <algorithm>
#include <utility>

namespace {

char const *kAbiHeader = R"(#pragma once
#ifdef __cplusplus
extern "C" {
#endif

typedef struct NrCpuImage {
  int width;
  int height;
  int stride;
  unsigned char *rgba;
} NrCpuImage;

int nr_process(NrCpuImage const *input, NrCpuImage *output);

#ifdef __cplusplus
}
#endif
)";

using ProcessFn = void *;

struct CpuImageC {
  int width;
  int height;
  int stride;
  unsigned char *rgba;
};

QString findGxx()
{
  QString const fromEnv = qEnvironmentVariable("NR_CXX");
  if (!fromEnv.isEmpty()) {
    return fromEnv;
  }
  QStringList const candidates = {
    QStringLiteral("g++"),
    QStringLiteral("C:/Qt/Tools/mingw1310_64/bin/g++.exe"),
    QStringLiteral("C:/Qt/Tools/mingw1120_64/bin/g++.exe"),
    QStringLiteral("C:/mingw64/bin/g++.exe"),
  };
  for (QString const &candidate : candidates) {
    if (QFileInfo::exists(candidate) || candidate == QLatin1String("g++")) {
      if (QFileInfo::exists(candidate)) {
        return candidate;
      }
    }
  }
  return QStringLiteral("g++");
}

QString kernelCacheDir()
{
  QString const root = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
  QDir dir(root.isEmpty() ? QDir::tempPath() : root);
  dir.mkpath(QStringLiteral("ofx-host/cpu-kernels"));
  return dir.filePath(QStringLiteral("ofx-host/cpu-kernels"));
}

class CpuRuntime final : public KernelRuntime
{
public:
  ~CpuRuntime() override { unload(); }

  bool load(QString const &source, QString *error) override
  {
    unload();

    QString const gxx = findGxx();
    QString const dir = kernelCacheDir();
    QString const token = QUuid::createUuid().toString(QUuid::Id128);
    QString const headerPath = QDir(dir).filePath(QStringLiteral("nr_cpu_abi.h"));
    QString const sourcePath = QDir(dir).filePath(token + QStringLiteral(".cpp"));
    m_dllPath = QDir(dir).filePath(token + QStringLiteral(".dll"));

    QFile header(headerPath);
    if (!header.open(QIODevice::WriteOnly | QIODevice::Truncate) || header.write(kAbiHeader) < 0) {
      if (error) {
        *error = QStringLiteral("ABI-Header konnte nicht geschrieben werden.");
      }
      return false;
    }
    header.close();

    QFile src(sourcePath);
    if (!src.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
      if (error) {
        *error = QStringLiteral("Kernel-Quelle konnte nicht geschrieben werden.");
      }
      return false;
    }
    src.write(source.toUtf8());
    src.close();

    QProcess compiler;
    compiler.setWorkingDirectory(dir);
    compiler.setProcessChannelMode(QProcess::MergedChannels);
    QStringList args;
    args << QStringLiteral("-shared") << QStringLiteral("-O2") << QStringLiteral("-std=c++20")
         << QStringLiteral("-I") << dir << sourcePath << QStringLiteral("-o") << m_dllPath;
    compiler.start(gxx, args);
    if (!compiler.waitForStarted(5000)) {
      if (error) {
        *error = QStringLiteral("g++ nicht gefunden. MinGW-g++ in PATH oder NR_CXX setzen.");
      }
      return false;
    }
    if (!compiler.waitForFinished(60000) || compiler.exitCode() != 0) {
      if (error) {
        *error = QString::fromLocal8Bit(compiler.readAll()).trimmed();
        if (error->isEmpty()) {
          *error = QStringLiteral("CPU-Kernel-Kompilierung fehlgeschlagen.");
        }
      }
      return false;
    }

    m_library.setFileName(m_dllPath);
    if (!m_library.load()) {
      if (error) {
        *error = QStringLiteral("DLL laden fehlgeschlagen: %1").arg(m_library.errorString());
      }
      return false;
    }
    m_layout = inferKernelPorts(NodeKind::Cpu, source);
    m_process = reinterpret_cast<ProcessFn>(m_library.resolve("nr_process"));
    if (!m_process) {
      if (error) {
        *error = QStringLiteral("Export nr_process fehlt in der DLL.");
      }
      unload();
      return false;
    }
    return true;
  }

  bool process(QVector<RgbaImage> const &inputs, QVector<RgbaImage> *outputs, QString *error) override
  {
    if (!m_process || !outputs || inputs.isEmpty()) {
      if (error) {
        *error = QStringLiteral("CPU-Kernel ist nicht geladen.");
      }
      return false;
    }

    int const nIn = std::min(m_layout.inCount, int(inputs.size()));
    int const nOut = m_layout.outCount;
    outputs->resize(nOut);
    QVector<CpuImageC> inImgs(nIn);
    QVector<CpuImageC> outImgs(nOut);
    for (int i = 0; i < nIn; ++i) {
      inImgs[i] = {inputs[i].width, inputs[i].height, inputs[i].width * 4,
                   const_cast<unsigned char *>(reinterpret_cast<unsigned char const *>(inputs[i].pixels.constData()))};
    }
    for (int i = 0; i < nOut; ++i) {
      (*outputs)[i] = inputs[0];
      outImgs[i] = {(*outputs)[i].width, (*outputs)[i].height, (*outputs)[i].width * 4,
                    reinterpret_cast<unsigned char *>((*outputs)[i].pixels.data())};
    }

    int rc = 1;
    if (nIn == 1 && nOut == 1) {
      using Fn = int (*)(CpuImageC const *, CpuImageC *);
      rc = reinterpret_cast<Fn>(m_process)(&inImgs[0], &outImgs[0]);
    } else if (nIn == 2 && nOut == 1) {
      using Fn = int (*)(CpuImageC const *, CpuImageC const *, CpuImageC *);
      rc = reinterpret_cast<Fn>(m_process)(&inImgs[0], &inImgs[1], &outImgs[0]);
    } else if (nIn == 3 && nOut == 1) {
      using Fn = int (*)(CpuImageC const *, CpuImageC const *, CpuImageC const *, CpuImageC *);
      rc = reinterpret_cast<Fn>(m_process)(&inImgs[0], &inImgs[1], &inImgs[2], &outImgs[0]);
    } else if (nIn == 2 && nOut == 2) {
      using Fn = int (*)(CpuImageC const *, CpuImageC const *, CpuImageC *, CpuImageC *);
      rc = reinterpret_cast<Fn>(m_process)(&inImgs[0], &inImgs[1], &outImgs[0], &outImgs[1]);
    } else if (nIn == 1 && nOut == 2) {
      using Fn = int (*)(CpuImageC const *, CpuImageC *, CpuImageC *);
      rc = reinterpret_cast<Fn>(m_process)(&inImgs[0], &outImgs[0], &outImgs[1]);
    } else {
      using Fn = int (*)(CpuImageC const *, int, CpuImageC *, int);
      rc = reinterpret_cast<Fn>(m_process)(inImgs.data(), nIn, outImgs.data(), nOut);
    }
    if (rc != 0) {
      if (error) {
        *error = QStringLiteral("nr_process gab %1 zurück.").arg(rc);
      }
      return false;
    }
    return true;
  }

private:
  void unload()
  {
    m_process = nullptr;
    if (m_library.isLoaded()) {
      m_library.unload();
    }
  }

  QLibrary m_library;
  QString m_dllPath;
  ProcessFn m_process = nullptr;
  KernelPortLayout m_layout;
};

} // namespace

std::unique_ptr<KernelRuntime> createCpuRuntime()
{
  return std::make_unique<CpuRuntime>();
}
