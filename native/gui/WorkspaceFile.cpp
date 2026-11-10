#include "gui/WorkspaceFile.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <cmath>
#include <limits>
#include <stdexcept>

namespace jlmol {
namespace {

constexpr qint64 maxFileBytes = 128 * 1024 * 1024;
constexpr int formatVersion = 1;
const auto formatName = QStringLiteral("jlmol-workspace");
const QStringList metrics{QStringLiteral("rmsd"), QStringLiteral("rmsf"), QStringLiteral("pca")};
const QStringList labels{QStringLiteral("none"), QStringLiteral("atoms"), QStringLiteral("residues")};
const QStringList lighting{QStringLiteral("standard"), QStringLiteral("soft"), QStringLiteral("flat")};

[[noreturn]] void fail(const QString& message) { throw std::runtime_error(message.toStdString()); }

[[noreturn]] void invalid(const QString& field) {
    fail(QStringLiteral("Invalid workspace field: %1").arg(field));
}

QString string(const QJsonValue& value, const QString& field) {
    const auto text = value.toString();
    if (text.isEmpty() || text.contains(QChar::Null)) invalid(field);
    return text;
}

QJsonObject object(const QJsonValue& value, const QString& field) {
    if (!value.isObject()) invalid(field);
    return value.toObject();
}

QJsonArray array(const QJsonValue& value, const QString& field) {
    if (!value.isArray()) invalid(field);
    return value.toArray();
}

qint64 integer(const QJsonValue& value, const QString& field, qint64 minimum, qint64 maximum) {
    const auto number = value.toInteger(-1);
    if (number < minimum || number > maximum) invalid(field);
    return number;
}

double real(const QJsonValue& value, const QString& field, double minimum, double maximum) {
    const auto number = value.toDouble(std::numeric_limits<double>::quiet_NaN());
    if (!std::isfinite(number) || number < minimum || number > maximum) invalid(field);
    return number;
}

bool boolean(const QJsonValue& value, const QString& field) {
    if (!value.isBool()) invalid(field);
    return value.toBool();
}

QString fileReference(const QDir& directory, const std::filesystem::path& path) {
    const QFileInfo info(path);
    if (!info.isFile() || !info.isReadable()) {
        fail(QStringLiteral("Workspace input is missing or unreadable: %1").arg(info.absoluteFilePath()));
    }
    const auto relative = directory.relativeFilePath(info.absoluteFilePath());
    return relative.startsWith(QStringLiteral("../")) ? info.absoluteFilePath() : relative;
}

std::filesystem::path inputFile(const QDir& directory, const QJsonValue& value, const QString& kind) {
    const auto path = directory.absoluteFilePath(string(value, kind));
    const QFileInfo info(path);
    if (!info.isFile() || !info.isReadable()) {
        fail(QStringLiteral("Missing or unreadable %1 file: %2").arg(kind, path));
    }
    return info.filesystemAbsoluteFilePath();
}

QJsonArray vector(const QVector3D& value) { return {value.x(), value.y(), value.z()}; }

template<std::size_t N>
std::array<float, N> components(const QJsonValue& value, const QString& field) {
    const auto values = array(value, field);
    if (values.size() != N) invalid(field);
    constexpr auto limit = std::numeric_limits<float>::max();
    std::array<float, N> result;
    for (std::size_t i = 0; i < N; ++i) result[i] = static_cast<float>(real(values[i], field, -limit, limit));
    return result;
}

QJsonObject saveCamera(const MolecularViewport::CameraState& camera) {
    const auto& q = camera.orientation;
    return {{"orientation", QJsonArray{q.scalar(), q.x(), q.y(), q.z()}}, {"pan", vector(camera.pan)},
            {"center", vector(camera.center)}, {"distance", camera.distance}, {"radius", camera.radius},
            {"orthographic", camera.orthographic}, {"labels", labels.at(static_cast<int>(camera.labels))},
            {"background", camera.background.name()}, {"lighting", lighting.at(static_cast<int>(camera.lighting))}};
}

MolecularViewport::CameraState loadCamera(const QJsonValue& value) {
    const auto fields = object(value, QStringLiteral("camera"));
    const auto [w, x, y, z] = components<4>(fields["orientation"], QStringLiteral("camera orientation"));
    const QQuaternion orientation(w, x, y, z);
    if (std::abs(orientation.lengthSquared() - 1.0F) > 0.001F) invalid(QStringLiteral("camera orientation"));
    const auto [px, py, pz] = components<3>(fields["pan"], QStringLiteral("camera pan"));
    const auto [cx, cy, cz] = components<3>(fields["center"], QStringLiteral("camera center"));
    const auto label = labels.indexOf(string(fields["labels"], QStringLiteral("camera labels")));
    if (label < 0) invalid(QStringLiteral("camera labels"));
    constexpr auto limit = std::numeric_limits<float>::max();
    MolecularViewport::CameraState camera{orientation, {px, py, pz}, {cx, cy, cz},
            static_cast<float>(real(fields["distance"], QStringLiteral("camera distance"), 1.0, limit)),
            static_cast<float>(real(fields["radius"], QStringLiteral("camera radius"), 1.0, limit)),
            boolean(fields["orthographic"], QStringLiteral("camera projection")), static_cast<LabelMode>(label)};
    if (fields.contains("background")) {
        const auto color = string(fields["background"], QStringLiteral("background"));
        camera.background = QColor(color);
        if (!camera.background.isValid() || camera.background.alpha() != 255) invalid(QStringLiteral("background"));
    }
    if (fields.contains("lighting")) {
        const auto preset = lighting.indexOf(string(fields["lighting"], QStringLiteral("lighting")));
        if (preset < 0) invalid(QStringLiteral("lighting"));
        camera.lighting = static_cast<LightingPreset>(preset);
    }
    return camera;
}

QJsonObject saveObject(const MolecularObject& entry, const QDir& directory) {
    QJsonObject fields{{"structure", fileReference(directory, entry.molecule->sourcePath())},
                       {"atoms", static_cast<qint64>(entry.molecule->atoms().size())}, {"visible", entry.visible},
                       {"representation", QString::fromStdString(toString(entry.representation))},
                       {"color", QString::fromStdString(toString(entry.colorScheme))}, {"opacity", entry.opacity}};
    if (entry.trajectory) {
        fields["trajectory"] = QJsonObject{{"path", fileReference(directory, entry.trajectory->path())},
                                           {"frame", static_cast<qint64>(entry.frameIndex)}};
    }
    if (entry.atomColors) {
        QJsonArray rgb;
        for (const auto& color : *entry.atomColors) for (const auto component : color) rgb.append(component);
        fields["rgb"] = rgb;
    }
    return fields;
}

ObjectId loadObject(const QJsonValue& value, const QDir& directory, MolecularScene& scene) {
    const auto fields = object(value, QStringLiteral("molecular object"));
    const auto path = inputFile(directory, fields["structure"], QStringLiteral("structure"));
    auto molecule = std::make_shared<Molecule>(Molecule::load(path));
    const auto count = integer(fields["atoms"], QStringLiteral("atom count"), 1, std::numeric_limits<qint64>::max());
    if (count != molecule->atoms().size()) {
        fail(QStringLiteral("Structure atom count changed: %1").arg(QFileInfo(path).absoluteFilePath()));
    }
    const auto id = scene.addMolecule(std::move(molecule));
    const auto representation = representationFromString(fields["representation"].toString().toStdString());
    if (!representation || !scene.setRepresentation(id, *representation)) invalid(QStringLiteral("representation"));
    (void)scene.setObjectVisible(id, boolean(fields["visible"], QStringLiteral("visibility")));
    (void)scene.setObjectOpacity(id, real(fields["opacity"], QStringLiteral("opacity"), 0.0, 1.0));
    const auto color = string(fields["color"], QStringLiteral("color scheme"));
    if (color == QStringLiteral("custom")) {
        const auto rgb = array(fields["rgb"], QStringLiteral("custom RGB palette"));
        if (rgb.size() % 3 != 0 || rgb.size() / 3 != count) invalid(QStringLiteral("custom RGB palette"));
        std::vector<float> colors;
        colors.reserve(rgb.size());
        for (const auto component : rgb) colors.push_back(static_cast<float>(real(component, QStringLiteral("RGB component"), 0.0, 1.0)));
        scene.setAtomColors(id, colors);
    } else {
        const auto scheme = colorSchemeFromString(color.toStdString());
        if (!scheme || !scene.setColorScheme(id, *scheme)) invalid(QStringLiteral("color scheme"));
    }
    if (fields.contains("trajectory")) {
        const auto trajectory = object(fields["trajectory"], QStringLiteral("trajectory"));
        const auto trajectoryPath = inputFile(directory, trajectory["path"], QStringLiteral("trajectory"));
        try {
            scene.attachTrajectory(id, trajectoryPath);
            const auto frameCount = scene.object(id)->trajectory->frameCount();
            const auto frame = integer(trajectory["frame"], QStringLiteral("trajectory frame"), 0,
                                       static_cast<qint64>(frameCount - 1));
            scene.setTrajectoryFrame(id, static_cast<std::size_t>(frame));
        } catch (const std::exception& error) {
            fail(QStringLiteral("Could not restore trajectory %1\n%2").arg(QFileInfo(trajectoryPath).absoluteFilePath(),
                                                                             QString::fromUtf8(error.what())));
        }
    }
    return id;
}

QJsonObject saveAnalysis(const WorkspaceSettings& settings) {
    const auto& trajectory = settings.trajectoryAnalysis;
    QJsonObject fields{{"trajectory", QJsonObject{{"metric", metrics.at(static_cast<int>(trajectory.metric))},
        {"align", trajectory.align}, {"selected_atoms", trajectory.selectedOnly}, {"reference", trajectory.reference},
        {"stride", trajectory.stride}, {"clusters", trajectory.clusters}, {"calculated", trajectory.calculated}}}};
    if (settings.contactMap) {
        const auto& map = *settings.contactMap;
        fields["contact_map"] = QJsonObject{{"cutoff", map.cutoff}, {"c_alpha_only", map.cAlphaOnly},
                                           {"frame", static_cast<qint64>(map.frame)}};
    }
    if (settings.alignmentReference) fields["alignment_reference"] = static_cast<qint64>(*settings.alignmentReference);
    return fields;
}

void loadAnalysis(const QJsonValue& value, const MolecularScene& scene, std::optional<std::size_t> activeIndex,
                  WorkspaceSettings& settings) {
    const auto fields = object(value, QStringLiteral("analysis"));
    const auto trajectory = object(fields["trajectory"], QStringLiteral("trajectory analysis"));
    const auto active = scene.activeObjectId();
    const auto entry = active ? scene.object(*active) : std::nullopt;
    const auto frameCount = entry && entry->trajectory ? entry->trajectory->frameCount() : 1;
    const auto maxFrame = static_cast<int>(std::min<std::size_t>(frameCount, std::numeric_limits<int>::max()));
    const auto metric = metrics.indexOf(string(trajectory["metric"], QStringLiteral("trajectory analysis metric")));
    if (metric < 0) invalid(QStringLiteral("trajectory analysis metric"));
    settings.trajectoryAnalysis = {static_cast<TrajectoryAnalysisPanel::Metric>(metric),
        boolean(trajectory["align"], QStringLiteral("trajectory alignment")),
        boolean(trajectory["selected_atoms"], QStringLiteral("trajectory atom selection")),
        static_cast<int>(integer(trajectory["reference"], QStringLiteral("analysis reference frame"), 1, maxFrame)),
        static_cast<int>(integer(trajectory["stride"], QStringLiteral("analysis stride"), 1, maxFrame)),
        static_cast<int>(integer(trajectory["clusters"], QStringLiteral("PCA clusters"), 0, 8)),
        boolean(trajectory["calculated"], QStringLiteral("trajectory calculation state"))};
    if (settings.trajectoryAnalysis.calculated && (!entry || !entry->trajectory)) invalid(QStringLiteral("trajectory calculation state"));
    if (fields.contains("contact_map")) {
        if (!entry) invalid(QStringLiteral("contact map structure"));
        const auto map = object(fields["contact_map"], QStringLiteral("contact map"));
        settings.contactMap = WorkspaceContactMap{real(map["cutoff"], QStringLiteral("contact map cutoff"), 0.01, 1000.0),
            boolean(map["c_alpha_only"], QStringLiteral("contact map atom filter")),
            static_cast<std::size_t>(integer(map["frame"], QStringLiteral("contact map frame"), 0, static_cast<qint64>(frameCount - 1)))};
    }
    if (fields.contains("alignment_reference")) {
        const auto index = integer(fields["alignment_reference"], QStringLiteral("alignment reference object"), 0,
                                   static_cast<qint64>(scene.objectCount()) - 1);
        if (!activeIndex || *activeIndex == index || settings.contactMap) invalid(QStringLiteral("alignment reference object"));
        settings.alignmentReference = static_cast<std::size_t>(index);
    }
}

} // namespace

void writeWorkspace(const QString& path, const MolecularScene& scene, const WorkspaceSettings& settings) {
    const QFileInfo destination(path);
    const auto directory = destination.absoluteDir();
    const auto existingTarget = destination.canonicalFilePath();
    const auto active = scene.activeObjectId();
    const auto selected = scene.selectedObjectId();
    QJsonValue activeIndex(QJsonValue::Null), selection(QJsonValue::Null);
    QJsonArray entries;
    for (const auto& entry : scene.objects()) {
        if (!existingTarget.isEmpty() && (QFileInfo(entry.molecule->sourcePath()).canonicalFilePath() == existingTarget ||
            (entry.trajectory && QFileInfo(entry.trajectory->path()).canonicalFilePath() == existingTarget))) {
            fail(QStringLiteral("Choose a workspace file separate from the referenced structure and trajectory files."));
        }
        const auto index = static_cast<qint64>(entries.size());
        if (entry.id == active) activeIndex = index;
        if (entry.id == selected) {
            QJsonArray atoms;
            for (const auto id : scene.selectedAtomIds()) atoms.append(QString::number(id));
            if (!atoms.isEmpty()) selection = QJsonObject{{"object", index}, {"atoms", atoms}};
        }
        entries.append(saveObject(entry, directory));
    }
    const QJsonObject root{{"format", formatName}, {"version", formatVersion}, {"objects", entries},
        {"active", activeIndex}, {"selection", selection}, {"camera", saveCamera(settings.camera)},
        {"layout", QString::fromLatin1(settings.layout.toBase64())}, {"playback_rate", settings.playbackRate},
        {"analysis", saveAnalysis(settings)}};
    const auto bytes = QJsonDocument(root).toJson(QJsonDocument::Compact);
    if (bytes.size() > maxFileBytes) fail(QStringLiteral("Workspace exceeds the 128 MiB size limit."));
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        fail(QStringLiteral("Could not save workspace %1\n%2").arg(path, file.errorString()));
    }
}

WorkspaceSettings readWorkspace(const QString& path, MolecularScene& staged) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) fail(QStringLiteral("Could not open workspace %1\n%2").arg(path, file.errorString()));
    const auto bytes = file.read(maxFileBytes + 1);
    if (file.error() != QFileDevice::NoError) fail(QStringLiteral("Could not read workspace %1\n%2").arg(path, file.errorString()));
    if (bytes.size() > maxFileBytes) fail(QStringLiteral("Workspace exceeds the 128 MiB size limit."));
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError) fail(QStringLiteral("Invalid workspace JSON: %1").arg(error.errorString()));
    const auto root = doc.object();
    if (root["format"].toString() != formatName) fail(QStringLiteral("This is not a JlMol workspace."));
    if (root["version"].toInteger() != formatVersion) fail(QStringLiteral("This workspace format version is unsupported."));

    WorkspaceSettings settings{};
    settings.camera = loadCamera(root["camera"]);
    auto layout = QByteArray::fromBase64Encoding(string(root["layout"], QStringLiteral("panel layout")).toLatin1(),
                                                 QByteArray::AbortOnBase64DecodingErrors);
    if (!layout || layout.decoded.isEmpty()) invalid(QStringLiteral("panel layout"));
    settings.layout = std::move(layout.decoded);
    settings.playbackRate = static_cast<int>(integer(root["playback_rate"], QStringLiteral("playback rate"), 1, 60));

    const auto entries = array(root["objects"], QStringLiteral("molecular objects"));
    std::optional<std::size_t> activeIndex;
    if (!entries.isEmpty() || !root["active"].isNull()) {
        activeIndex = static_cast<std::size_t>(integer(root["active"], QStringLiteral("active object"), 0, entries.size() - 1));
    }
    const auto directory = QFileInfo(path).absoluteDir();
    std::vector<ObjectId> ids;
    ids.reserve(entries.size());
    for (const auto& entry : entries) ids.push_back(loadObject(entry, directory, staged));

    if (!root["selection"].isNull()) {
        const auto selection = object(root["selection"], QStringLiteral("selection"));
        const auto index = integer(selection["object"], QStringLiteral("selected object"), 0, entries.size() - 1);
        const auto atoms = array(selection["atoms"], QStringLiteral("selected atoms"));
        std::vector<std::uint64_t> selected;
        selected.reserve(atoms.size());
        for (const auto atom : atoms) {
            bool ok = false;
            const auto id = atom.toString().toULongLong(&ok);
            if (!ok || id == 0) invalid(QStringLiteral("selected atom ID"));
            selected.push_back(id);
        }
        staged.selectAtoms(ids[index], selected);
        if (selected.empty() || staged.selectedAtomIds() != selected) invalid(QStringLiteral("selected atoms"));
    }
    if (activeIndex) (void)staged.setActiveObject(ids[*activeIndex]);
    loadAnalysis(root["analysis"], staged, activeIndex, settings);
    return settings;
}

} // namespace jlmol
