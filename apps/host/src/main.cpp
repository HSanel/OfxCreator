#include "MainWindow.h"

#include <QApplication>
#include <QSurfaceFormat>

int main(int argc, char *argv[])
{
  QSurfaceFormat format;
  format.setSamples(4);
  QSurfaceFormat::setDefaultFormat(format);

  QApplication app(argc, argv);
  app.setApplicationName(QStringLiteral("NR OFX Host"));
  app.setOrganizationName(QStringLiteral("NR"));

  MainWindow window;
  window.show();
  return app.exec();
}
