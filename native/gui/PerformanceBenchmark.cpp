#include "gui/MainWindow.h"

#include "gui/MolecularViewport.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QMouseEvent>
#include <QSlider>
#include <QTemporaryDir>
#include <QTextStream>

#include <algorithm>
#include <cmath>
#include <vector>

namespace jlmol {
namespace {

constexpr int samples = 7;
constexpr int frameCount = 8;

Vec3 gridPoint(int index, int side, int frame = 0) {
    return {3.F * (index % side) + 0.15F * frame, 3.F * ((index / side) % side), 3.F * (index / (side * side))};
}

bool writeFixtures(const QString& topology, const QString& trajectory, int atoms) {
    const int side = static_cast<int>(std::ceil(std::cbrt(atoms)));
    QFile structure(topology), frames(trajectory);
    if (!structure.open(QIODevice::WriteOnly) || !frames.open(QIODevice::WriteOnly)) return false;
    QTextStream mol2(&structure), xyz(&frames);
    const int residues = atoms / 10;
    mol2 << "@<TRIPOS>MOLECULE\nPerformance benchmark\n" << atoms << ' ' << atoms - residues << ' ' << residues
         << " 0 0\nSMALL\nNO_CHARGES\n\n@<TRIPOS>ATOM\n";
    for (int i = 0; i < atoms; ++i) {
        const auto p = gridPoint(i, side);
        mol2 << i + 1 << " C " << p.x << ' ' << p.y << ' ' << p.z << " C.3 " << i / 10 + 1 << " SYN\n";
    }
    mol2 << "@<TRIPOS>BOND\n";
    int bond = 0;
    for (int i = 1; i < atoms; ++i) {
        if (i % 10) mol2 << ++bond << ' ' << i << ' ' << i + 1 << " 1\n";
    }
    for (int frame = 0; frame < frameCount; ++frame) {
        xyz << atoms << "\nBenchmark frame " << frame + 1 << '\n';
        for (int i = 0; i < atoms; ++i) {
            const auto p = gridPoint(i, side, frame);
            xyz << "C " << p.x << ' ' << p.y << ' ' << p.z << '\n';
        }
    }
    mol2.flush();
    xyz.flush();
    return mol2.status() == QTextStream::Ok && xyz.status() == QTextStream::Ok &&
        structure.error() == QFileDevice::NoError && frames.error() == QFileDevice::NoError;
}

struct Sample {
    double updateMs{};
    MolecularViewport::RedrawTiming redraw;
};

double median(std::vector<double>& values) {
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

void writeRow(QTextStream& report, int atoms, const char* operation, const std::vector<Sample>& values) {
    std::vector<double> update, geometry, upload, redraw, total;
    std::size_t bytes = 0;
    for (const auto& sample : values) {
        update.push_back(sample.updateMs);
        geometry.push_back(sample.redraw.geometryMs);
        upload.push_back(sample.redraw.uploadMs);
        redraw.push_back(sample.redraw.totalMs);
        total.push_back(sample.updateMs + sample.redraw.totalMs);
        bytes = std::max(bytes, sample.redraw.uploadBytes);
    }
    report << atoms << '\t' << operation << '\t' << median(update) << '\t' << median(geometry) << '\t'
           << median(upload) << '\t' << median(redraw) << '\t' << median(total) << '\t' << bytes << '\n';
}

} // namespace

bool MainWindow::runPerformanceBenchmark(QString* report) {
    QTemporaryDir directory;
    if (!directory.isValid()) return false;
    QTextStream output(report);
    output.setRealNumberNotation(QTextStream::FixedNotation);
    output.setRealNumberPrecision(3);
    output << "atoms\toperation\tupdate_ms\tgeometry_ms\tupload_ms\tredraw_ms\ttotal_ms\tupload_bytes_max\n";
    QCoreApplication::processEvents();
    if (!viewport_->isValid()) {
        output << "The benchmark requires a native OpenGL window.\n";
        return false;
    }
    output << "# Viewport " << viewport_->width() << " x " << viewport_->height()
           << " logical pixels, device scale " << viewport_->devicePixelRatioF() << '\n';
    const auto measure = [&](auto&& update) {
        QElapsedTimer clock;
        clock.start();
        update();
        const auto updateMs = clock.nsecsElapsed() / 1.0e6;
        return Sample{updateMs, viewport_->measureRedraw()};
    };
    const auto series = [&](int atoms, const char* operation, auto&& update) {
        std::vector<Sample> values;
        values.reserve(samples);
        for (int i = 0; i < samples; ++i) values.push_back(measure([&] { update(i); }));
        writeRow(output, atoms, operation, values);
    };

    for (const int atoms : {1000, 10000, 100000}) {
        const auto topology = directory.filePath(QStringLiteral("%1.mol2").arg(atoms));
        const auto trajectory = directory.filePath(QStringLiteral("%1.xyz").arg(atoms));
        if (!writeFixtures(topology, trajectory, atoms)) return false;
        bool opened = false;
        const auto imported = measure([&] { opened = openStructure(topology); });
        const auto id = scene_->activeObjectId();
        if (!opened || !id || scene_->molecule()->atoms().size() != static_cast<std::size_t>(atoms)) return false;
        writeRow(output, atoms, "open", {imported});
        series(atoms, "idle", [](int) {});
        series(atoms, "color", [&](int i) { dispatcher_->setColorScheme(i % 2 ? ColorScheme::Element : ColorScheme::Chain); });
        dispatcher_->setColorScheme(ColorScheme::Element);
        (void)viewport_->measureRedraw();
        series(atoms, "pick", [&](int i) {
            const QPointF position(viewport_->width() / 2.0 + (i - 3) * 4, viewport_->height() / 2.0);
            const QPointF global = viewport_->mapToGlobal(position.toPoint());
            QMouseEvent press(QEvent::MouseButtonPress, position, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QMouseEvent release(QEvent::MouseButtonRelease, position, global, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QCoreApplication::sendEvent(viewport_, &press);
            QCoreApplication::sendEvent(viewport_, &release);
        });
        if (scene_->selectedObjectId() != id) {
            output << "The picking probe did not hit the fixture.\n";
            return false;
        }
        const auto molecule = scene_->molecule();
        const int groupSize = std::min(atoms / 10, 500);
        series(atoms, "select_group", [&](int i) {
            std::vector<std::uint64_t> ids;
            ids.reserve(groupSize);
            for (int j = atoms - groupSize - i; j < atoms - i; ++j) ids.push_back(molecule->atoms()[j].id);
            dispatcher_->selectAtoms(*id, std::move(ids));
        });
        dispatcher_->selectAtom(std::nullopt);

        bool attached = false;
        const auto attachment = measure([&] { attached = attachTrajectory(trajectory); });
        if (!attached) return false;
        writeRow(output, atoms, "attach", {attachment});
        bool seeksOk = true;
        const auto seek = [&](int frame) { seeksOk &= dispatcher_->setTrajectoryFrame(*id, frame); };
        series(atoms, "play_uncached", [&](int i) { seek(i + 1); });
        series(atoms, "play_cached", seek);
        if (!seeksOk) return false;
        const auto previous = scene_->object(*id)->trajectory;
        QElapsedTimer attachmentClock;
        attachmentClock.start();
        dispatcher_->requestTrajectoryAttachment(*id, trajectory.toStdString());
        const auto attachSubmit = attachmentClock.nsecsElapsed() / 1.0e6;
        if (!waitForTrajectoryAttachment(*id, previous)) return false;
        output << "# Async attachment at " << atoms << " atoms: submit " << attachSubmit
               << " ms, first frame ready " << attachmentClock.nsecsElapsed() / 1.0e6 << " ms\n";

        auto* timeline = findChild<QSlider*>(QStringLiteral("TrajectorySlider"));
        if (!timeline) return false;
        std::vector<double> submitTimes, readyTimes;
        for (int frame = 1; frame < frameCount; ++frame) {
            QElapsedTimer clock;
            clock.start();
            timeline->setValue(frame + 1);
            submitTimes.push_back(clock.nsecsElapsed() / 1.0e6);
            if (!waitForTrajectoryFrame(*id, frame)) return false;
            readyTimes.push_back(clock.nsecsElapsed() / 1.0e6);
            (void)viewport_->measureRedraw();
        }
        output << "# Async uncached seek at " << atoms << " atoms: submit " << median(submitTimes)
               << " ms, frame ready " << median(readyTimes) << " ms\n";
        if (!dispatcher_->closeObject(*id)) return false;
        (void)viewport_->measureRedraw();
    }
    return true;
}

} // namespace jlmol
