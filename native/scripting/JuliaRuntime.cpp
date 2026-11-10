#include "scripting/JuliaRuntime.h"
#include "app/ApplicationPaths.h"

#include <QByteArray>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <string>

#if defined(JLMOL_HAS_JULIA)
#  include <julia.h>
JULIA_DEFINE_FAST_TLS
#endif

namespace jlmol {

JuliaRuntime::~JuliaRuntime() { shutdown(); }

bool JuliaRuntime::initialize(QString* error) {
#if defined(JLMOL_HAS_JULIA)
    if (available_) return true;
    try {
        const auto paths = applicationPaths();
        if (paths.bundled) {
            if (!QFileInfo(QDir(paths.project).filePath(QStringLiteral("Project.toml"))).isFile() ||
                !QFileInfo(paths.coreLibrary).isFile() || !QFileInfo(paths.juliaBin).isDir()) {
                if (error) *error = QStringLiteral("The application bundle is missing its Julia runtime or project files.");
                return false;
            }
            if (!qEnvironmentVariableIsSet("JULIA_DEPOT_PATH")) {
                const auto data = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
                const auto depot = QDir(data).filePath(QStringLiteral("julia-depot"));
                if (data.isEmpty() || !QDir().mkpath(depot)) {
                    if (error) *error = QStringLiteral("Could not create the JlMol Julia depot at %1.").arg(depot);
                    return false;
                }
                // The trailing separator keeps Julia's bundled system depots available.
                qputenv("JULIA_DEPOT_PATH", depot.toUtf8() + QDir::listSeparator().toLatin1());
            }
            if (!qEnvironmentVariableIsSet("JULIA_LOAD_PATH")) {
                qputenv("JULIA_LOAD_PATH", QByteArray("@") + QDir::listSeparator().toLatin1() + "@stdlib");
            }
        }
        qputenv("JLMOL_CORE_LIBRARY", paths.coreLibrary.toUtf8());
        qputenv("JULIA_PROJECT", paths.project.toUtf8());
        const auto juliaBin = paths.juliaBin.toUtf8();
        jl_init_with_image_file(juliaBin.constData(), nullptr);
        ownsRuntime_ = true;
        const char* bootstrap = R"JULIA(
            module JlMolSession end
            module JlMolAnalysisSession end
            function __jlmol_plugin__(operation::String, argument::String, item::String)
                try
                    package = Base.require(Main, :JlMol)
                    return Base.invokelatest(getproperty(package, :_plugin_request), operation, argument, item)::Vector{String}
                catch exception
                    return sprint(showerror, exception)
                end
            end
            function __jlmol_series__(source::String)
                try
                    return Base.include_string(Main.JlMolAnalysisSession, source, "JlMol analysis")::Vector{Float64}
                catch exception
                    return sprint(showerror, exception)
                end
            end
            function __jlmol_eval__(source::String)
                stream = Pipe()
                try
                    value = redirect_stdout(stream) do
                        redirect_stderr(stream) do
                            Base.include_string(Main.JlMolSession, source, "JlMol console")
                        end
                    end
                    close(stream.in)
                    output = read(stream, String)
                    value === nothing ? output : output * sprint(show, value)
                catch exception
                    isopen(stream.in) && close(stream.in)
                    output = read(stream, String)
                    output * sprint(Base.showerror, exception, catch_backtrace())
                end
            end
        )JULIA";
        jl_eval_string(bootstrap);
        if (jl_exception_occurred()) {
            if (error) *error = QStringLiteral("Julia bootstrap failed");
            shutdown();
            return false;
        }
        jl_value_t* version = jl_eval_string("string(VERSION)");
        if (version && jl_is_string(version)) {
            version_ = QString::fromUtf8(jl_string_ptr(version));
        }
        available_ = true;
        return true;
    } catch (...) {
        if (error) *error = QStringLiteral("Julia runtime initialization threw an unexpected native exception");
        shutdown();
        return false;
    }
#else
    if (error) *error = QStringLiteral("This build was compiled without an embedded Julia runtime");
    return false;
#endif
}

QString JuliaRuntime::evaluate(const QString& source) {
#if defined(JLMOL_HAS_JULIA)
    std::scoped_lock lock(evaluationMutex_);
    if (!available_) return QStringLiteral("Julia is not available.");
    const QByteArray utf8 = source.toUtf8();
    jl_value_t* argument = jl_cstr_to_string(utf8.constData());
    JL_GC_PUSH1(&argument);
    jl_value_t* evaluator = jl_get_function(jl_main_module, "__jlmol_eval__");
    jl_value_t* result = evaluator ? jl_call1(evaluator, argument) : nullptr;
    QString output;
    if (jl_exception_occurred()) {
        jl_value_t* exception = jl_exception_occurred();
        jl_value_t* showerror = jl_get_function(jl_base_module, "showerror");
        jl_value_t* rendered = showerror ? jl_call1(showerror, exception) : nullptr;
        output = rendered && jl_is_string(rendered) ? QString::fromUtf8(jl_string_ptr(rendered))
                                                    : QStringLiteral("Julia evaluation failed");
    } else if (result && jl_is_string(result)) {
        output = QString::fromUtf8(jl_string_ptr(result));
    }
    JL_GC_POP();
    return output;
#else
    (void)source;
    return QStringLiteral("Julia is not available in this build.");
#endif
}

NumericSeries JuliaRuntime::evaluateSeries(const QString& source) {
#if defined(JLMOL_HAS_JULIA)
    std::scoped_lock lock(evaluationMutex_);
    if (!available_) return {{}, QStringLiteral("Julia is not available.")};
    const auto utf8 = source.toUtf8();
    jl_value_t* argument = jl_cstr_to_string(utf8.constData());
    jl_value_t* result = nullptr;
    JL_GC_PUSH2(&argument, &result);
    NumericSeries series;
    try {
        auto* evaluator = jl_get_function(jl_main_module, "__jlmol_series__");
        result = evaluator ? jl_call1(evaluator, argument) : nullptr;
        if (!result || jl_exception_occurred()) {
            series.error = QStringLiteral("Julia analysis did not return a result.");
        } else if (jl_is_string(result)) {
            series.error = QString::fromUtf8(jl_string_ptr(result));
        } else if (jl_is_array(result) && jl_array_ndims(result) == 1 && jl_array_eltype(result) == jl_float64_type) {
            auto* array = reinterpret_cast<jl_array_t*>(result);
            const auto size = jl_array_len(array);
#if JULIA_VERSION_MAJOR > 1 || JULIA_VERSION_MINOR >= 11
            const auto* values = jl_array_data(array, double);
#else
            const auto* values = static_cast<const double*>(jl_array_data(array));
#endif
            if (size > 0) series.values.assign(values, values + size);
        } else {
            series.error = QStringLiteral("Julia analysis did not return a Float64 vector.");
        }
    } catch (...) {
        JL_GC_POP();
        throw;
    }
    JL_GC_POP();
    return series;
#else
    (void)source;
    return {{}, QStringLiteral("Julia is not available in this build.")};
#endif
}

PluginResponse JuliaRuntime::pluginRequest(const QString& operation, const QString& argument, const QString& item) {
#if defined(JLMOL_HAS_JULIA)
    std::scoped_lock lock(evaluationMutex_);
    if (!available_) return {{}, QStringLiteral("Julia is not available.")};
    const auto request = operation.toUtf8();
    const auto target = argument.toUtf8();
    const auto detail = item.toUtf8();
    jl_value_t* op = nullptr;
    jl_value_t* arg = nullptr;
    jl_value_t* key = nullptr;
    jl_value_t* result = nullptr;
    JL_GC_PUSH4(&op, &arg, &key, &result);
    PluginResponse response;
    try {
        op = jl_pchar_to_string(request.constData(), request.size());
        arg = jl_pchar_to_string(target.constData(), target.size());
        key = jl_pchar_to_string(detail.constData(), detail.size());
        auto* evaluator = jl_get_function(jl_main_module, "__jlmol_plugin__");
        result = evaluator ? jl_call3(evaluator, op, arg, key) : nullptr;
        const auto decode = [](jl_value_t* string) {
            return QString::fromUtf8(jl_string_ptr(string), static_cast<qsizetype>(jl_string_len(string)));
        };
        if (!result || jl_exception_occurred()) {
            response.error = QStringLiteral("Julia did not complete the plugin request.");
        } else if (jl_is_string(result)) {
            response.error = decode(result);
        } else if (jl_is_array(result) && jl_array_ndims(result) == 1 && jl_array_eltype(result) == jl_string_type) {
            auto* array = reinterpret_cast<jl_array_t*>(result);
            for (std::size_t i = 0; i < jl_array_len(array); ++i) {
                auto* field = jl_array_ptr_ref(array, i);
                if (!field) {
                    response = {{}, QStringLiteral("Julia returned an incomplete plugin response.")};
                    break;
                }
                response.fields.append(decode(field));
            }
        } else {
            response.error = QStringLiteral("Julia returned an invalid plugin response.");
        }
    } catch (...) {
        JL_GC_POP();
        throw;
    }
    JL_GC_POP();
    return response;
#else
    (void)operation;
    (void)argument;
    (void)item;
    return {{}, QStringLiteral("Julia is not available in this build.")};
#endif
}

void JuliaRuntime::shutdown() {
#if defined(JLMOL_HAS_JULIA)
    if (ownsRuntime_) {
        available_ = false;
        jl_atexit_hook(0);
        ownsRuntime_ = false;
    }
#endif
}

} // namespace jlmol

