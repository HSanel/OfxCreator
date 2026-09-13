#include "Pipeline.h"

#include <QJsonArray>

QJsonObject Pipeline::toJson() const
{
  QJsonObject root;
  root.insert(QStringLiteral("format"), QString::fromLatin1(kFormatId));
  root.insert(QStringLiteral("version"), kFormatVersion);
  root.insert(QStringLiteral("currentFrame"), currentFrame);

  QJsonArray nodeArray;
  for (PipelineNode const &node : nodes) {
    QJsonObject json;
    json.insert(QStringLiteral("id"), static_cast<qint64>(node.id));
    json.insert(QStringLiteral("type"), nodeKindId(node.kind));
    QJsonObject position;
    position.insert(QStringLiteral("x"), node.x);
    position.insert(QStringLiteral("y"), node.y);
    json.insert(QStringLiteral("position"), position);
    json.insert(QStringLiteral("params"), node.params);
    nodeArray.append(json);
  }
  root.insert(QStringLiteral("nodes"), nodeArray);

  QJsonArray connectionArray;
  for (PipelineConnection const &connection : connections) {
    QJsonObject json;
    json.insert(QStringLiteral("from"), static_cast<qint64>(connection.fromId));
    json.insert(QStringLiteral("fromPort"), connection.fromPort);
    json.insert(QStringLiteral("to"), static_cast<qint64>(connection.toId));
    json.insert(QStringLiteral("toPort"), connection.toPort);
    connectionArray.append(json);
  }
  root.insert(QStringLiteral("connections"), connectionArray);
  return root;
}

Pipeline Pipeline::fromJson(QJsonObject const &json, QString *error)
{
  Pipeline pipeline;
  if (json.value(QStringLiteral("format")).toString() != QLatin1String(kFormatId)) {
    if (error) {
      *error = QStringLiteral("Unbekanntes Pipeline-Format.");
    }
    return {};
  }

  int const version = json.value(QStringLiteral("version")).toInt();
  if (version < 1 || version > kFormatVersion) {
    if (error) {
      *error = QStringLiteral("Nicht unterstützte Pipeline-Version.");
    }
    return {};
  }

  pipeline.currentFrame = json.value(QStringLiteral("currentFrame")).toInt(0);

  for (QJsonValue const &value : json.value(QStringLiteral("nodes")).toArray()) {
    QJsonObject const nodeJson = value.toObject();
    PipelineNode node;
    node.id = static_cast<quint64>(nodeJson.value(QStringLiteral("id")).toInteger());
    node.kind = nodeKindFromId(nodeJson.value(QStringLiteral("type")).toString());
    QJsonObject const position = nodeJson.value(QStringLiteral("position")).toObject();
    node.x = position.value(QStringLiteral("x")).toDouble();
    node.y = position.value(QStringLiteral("y")).toDouble();
    node.params = nodeJson.value(QStringLiteral("params")).toObject();
    if (node.kind == NodeKind::Unknown) {
      if (error) {
        *error = QStringLiteral("Unbekannter Knotentyp in der Datei.");
      }
      return {};
    }
    pipeline.nodes.push_back(node);
  }

  for (QJsonValue const &value : json.value(QStringLiteral("connections")).toArray()) {
    QJsonObject const connectionJson = value.toObject();
    PipelineConnection connection;
    connection.fromId = static_cast<quint64>(connectionJson.value(QStringLiteral("from")).toInteger());
    connection.fromPort = connectionJson.value(QStringLiteral("fromPort")).toInt();
    connection.toId = static_cast<quint64>(connectionJson.value(QStringLiteral("to")).toInteger());
    connection.toPort = connectionJson.value(QStringLiteral("toPort")).toInt();
    pipeline.connections.push_back(connection);
  }

  return pipeline;
}
