#include "PortInference.h"

#include <QRegularExpression>
#include <QStringList>
#include <QVector>

#include <algorithm>

namespace {

QString stripBlockComments(QString text)
{
  static QRegularExpression const block(QStringLiteral(R"(/\*.*?\*/)"),
                                        QRegularExpression::DotMatchesEverythingOption);
  return text.remove(block);
}

QString extractArgs(QString const &source, QRegularExpression const &fn)
{
  QRegularExpressionMatch const match = fn.match(source);
  if (!match.hasMatch()) {
    return {};
  }
  int pos = match.capturedEnd();
  int depth = 1;
  QString args;
  for (; pos < source.size(); ++pos) {
    QChar const ch = source.at(pos);
    if (ch == QLatin1Char('(')) {
      ++depth;
    } else if (ch == QLatin1Char(')')) {
      --depth;
      if (depth == 0) {
        break;
      }
    }
    if (depth >= 1) {
      args.append(ch);
    }
  }
  return args;
}

QStringList splitArgs(QString const &args)
{
  QStringList parts;
  QString current;
  int depth = 0;
  for (QChar const ch : args) {
    if (ch == QLatin1Char('(') || ch == QLatin1Char('<') || ch == QLatin1Char('[')) {
      ++depth;
      current.append(ch);
    } else if (ch == QLatin1Char(')') || ch == QLatin1Char('>') || ch == QLatin1Char(']')) {
      depth = std::max(0, depth - 1);
      current.append(ch);
    } else if (ch == QLatin1Char(',') && depth == 0) {
      parts.push_back(current.trimmed());
      current.clear();
    } else {
      current.append(ch);
    }
  }
  if (!current.trimmed().isEmpty()) {
    parts.push_back(current.trimmed());
  }
  return parts;
}

bool isScalarArg(QString const &arg)
{
  static QRegularExpression const scalar(
    QStringLiteral(R"(^(?:const\s+)?(?:unsigned\s+)?(?:int|uint|float|double|size_t|cl_int|cl_uint)\b)"),
    QRegularExpression::CaseInsensitiveOption);
  return scalar.match(arg).hasMatch() && !arg.contains(QLatin1Char('*'));
}

QString argName(QString arg)
{
  arg.replace(QLatin1Char('*'), QLatin1Char(' '));
  arg.replace(QLatin1Char('&'), QLatin1Char(' '));
  QStringList const tokens = arg.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
  return tokens.isEmpty() ? QString() : tokens.last();
}

bool nameLooksOutput(QString const &name)
{
  QString const n = name.toLower();
  return n.contains(QLatin1String("out")) || n.contains(QLatin1String("dst"))
         || n.contains(QLatin1String("dest")) || n.contains(QLatin1String("result"));
}

bool nameLooksInput(QString const &name)
{
  QString const n = name.toLower();
  return n.contains(QLatin1String("in")) || n.contains(QLatin1String("src"))
         || n.contains(QLatin1String("img")) || n.contains(QLatin1String("rgba"))
         || n.contains(QLatin1String("mask")) || n.contains(QLatin1String("ref"))
         || n == QLatin1String("a") || n == QLatin1String("b");
}

KernelPortLayout fromPointerArgs(QStringList const &args)
{
  struct PtrArg {
    bool isConst = false;
    QString name;
  };
  QVector<PtrArg> pointers;
  for (QString const &arg : args) {
    if (arg.isEmpty() || isScalarArg(arg)) {
      continue;
    }
    bool const isPtr = arg.contains(QLatin1Char('*')) || arg.contains(QLatin1String("NrCpuImage"))
                       || arg.contains(QLatin1String("image"));
    if (!isPtr) {
      continue;
    }
    PtrArg p;
    p.isConst = arg.contains(QRegularExpression(QStringLiteral("\\bconst\\b")));
    p.name = argName(arg);
    pointers.push_back(p);
  }

  KernelPortLayout layout;
  if (pointers.isEmpty()) {
    return layout;
  }

  bool namedOut = false;
  bool namedIn = false;
  for (PtrArg const &p : pointers) {
    namedOut = namedOut || nameLooksOutput(p.name);
    namedIn = namedIn || nameLooksInput(p.name);
  }

  int inCount = 0;
  int outCount = 0;
  if (namedOut || namedIn) {
    for (PtrArg const &p : pointers) {
      if (nameLooksOutput(p.name) && !nameLooksInput(p.name)) {
        ++outCount;
      } else if (nameLooksInput(p.name) && !nameLooksOutput(p.name)) {
        ++inCount;
      } else if (p.isConst) {
        ++inCount;
      } else if (nameLooksOutput(p.name)) {
        ++outCount;
      } else {
        ++inCount;
      }
    }
    if (outCount == 0) {
      outCount = 1;
      inCount = std::max(1, int(pointers.size()) - 1);
    }
  } else {
    bool anyConst = false;
    for (PtrArg const &p : pointers) {
      anyConst = anyConst || p.isConst;
    }
    if (anyConst) {
      for (PtrArg const &p : pointers) {
        if (p.isConst) {
          ++inCount;
        } else {
          ++outCount;
        }
      }
    } else {
      outCount = 1;
      inCount = std::max(1, int(pointers.size()) - 1);
    }
  }

  layout.inCount = std::clamp(inCount, 1, 4);
  layout.outCount = std::clamp(std::max(1, outCount), 1, 4);
  return layout;
}

KernelPortLayout fromPython(QString const &source)
{
  KernelPortLayout layout;
  static QRegularExpression const defFn(QStringLiteral(R"(\bdef\s+process\s*\()"));
  QString const args = extractArgs(source, defFn);
  if (args.isEmpty()) {
    return layout;
  }
  int inputs = 0;
  for (QString arg : splitArgs(args)) {
    arg = arg.trimmed();
    int const eq = arg.indexOf(QLatin1Char('='));
    if (eq >= 0) {
      arg = arg.left(eq).trimmed();
    }
    arg.remove(QRegularExpression(QStringLiteral(R"(^\*+|\s*:.*)")));
    if (arg.isEmpty() || arg == QLatin1String("self") || arg == QLatin1String("width")
        || arg == QLatin1String("height") || arg == QLatin1String("w") || arg == QLatin1String("h")) {
      continue;
    }
    ++inputs;
  }
  layout.inCount = std::clamp(std::max(1, inputs), 1, 4);

  static QRegularExpression const ret(QStringLiteral(R"(\breturn\s+(.+))"));
  QRegularExpressionMatchIterator it = ret.globalMatch(source);
  QString last;
  while (it.hasNext()) {
    last = it.next().captured(1).trimmed();
  }
  if (last.startsWith(QLatin1Char('(')) && last.contains(QLatin1Char(','))) {
    layout.outCount = std::clamp(int(last.count(QLatin1Char(',')) + 1), 1, 4);
  }
  return layout;
}

bool fromDirective(QString const &source, KernelPortLayout *layout)
{
  static QRegularExpression const dir(
    QStringLiteral(R"(nr[-_]?ports\s*[:=]\s*in\s*=\s*(\d+)\s*,?\s*out\s*=\s*(\d+))"),
    QRegularExpression::CaseInsensitiveOption);
  QRegularExpressionMatch const match = dir.match(source);
  if (!match.hasMatch()) {
    return false;
  }
  layout->inCount = std::clamp(match.captured(1).toInt(), 1, 4);
  layout->outCount = std::clamp(match.captured(2).toInt(), 1, 4);
  return true;
}

} // namespace

KernelPortLayout inferKernelPorts(NodeKind kind, QString const &source)
{
  KernelPortLayout layout;
  QString const text = stripBlockComments(source);
  if (fromDirective(text, &layout)) {
    return layout;
  }

  if (kind == NodeKind::Python) {
    return fromPython(text);
  }

  QRegularExpression fn;
  if (kind == NodeKind::Cpu) {
    fn.setPattern(QStringLiteral(R"(\bnr_process\s*\()"));
  } else {
    fn.setPattern(QStringLiteral(R"(\bprocess\s*\()"));
  }
  QString const args = extractArgs(text, fn);
  if (!args.isEmpty()) {
    layout = fromPointerArgs(splitArgs(args));
  }
  return layout;
}
