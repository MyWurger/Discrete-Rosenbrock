// ============================================================================
// ФАЙЛ MAIN.CPP - ТОЧКА ВХОДА В QT-ПРИЛОЖЕНИЕ LAB_2
// ============================================================================
// Назначение файла:
// 1) Подготовить среду выполнения Qt перед запуском GUI.
// 2) Создать основные сервисы приложения: оптимизатор и парсер функции.
// 3) Создать и показать главное окно программы.
// ============================================================================

// Подключаем главное окно приложения.
#include "lab2/MainWindow.h"
// Подключаем реализацию парсера функции через muparser.
#include "lab2/MuParserObjectiveParser.h"
// Подключаем реализацию дискретного метода Розенброка.
#include "lab2/RosenbrockDiscreteMethod.h"

// QApplication нужен для запуска цикла обработки событий Qt.
#include <QApplication>
// QDir нужен для работы с каталогами и построения путей к plugin-папкам.
#include <QDir>
// QFileInfo нужен для проверки существования файла платформенного plugin-а.
#include <QFileInfo>
// QLibraryInfo нужен для получения стандартного пути к Qt plugin-ам.
#include <QLibraryInfo>
// QProcess нужен для запроса `brew --prefix qtbase` на macOS.
#include <QProcess>
// QString нужен для строковых операций Qt.
#include <QString>
// QStringList нужен для списков путей-кандидатов.
#include <QStringList>

namespace
{

// ============================================================================
// ФУНКЦИЯ HasCocoaPlugin - ПРОВЕРКА НАЛИЧИЯ QT-PLUGIN "COCOA"
// ============================================================================
// На macOS приложение Qt должно уметь найти платформенный plugin cocoa.
// Эта функция проверяет, есть ли файл libqcocoa.dylib в переданной папке
// platforms.
// Принимает:
// - platformsPath: путь до каталога с платформенными plugin-ами Qt.
// Возвращает:
// - true, если plugin найден;
// - false, если путь пуст или файл отсутствует.
// ============================================================================
bool HasCocoaPlugin(const QString& platformsPath)
{
    // Пустой путь сразу считаем невалидным кандидатом.
    // Метод isEmpty() в Qt проверяет именно длину строки, а не существование пути на диске.
    if (platformsPath.isEmpty())
    {
        return false;
    }

    // Формируем полный путь до платформенного plugin-а cocoa внутри каталога platforms.
    // QString::fromUtf8(...) используем явно, чтобы строковый литерал гарантированно
    // интерпретировался как UTF-8 и одинаково вел себя на разных платформах.
    const QString pluginFile = QDir(platformsPath).filePath(QString::fromUtf8("libqcocoa.dylib"));
    // Создаем QFileInfo, чтобы проверить существование и тип объекта на диске.
    // QFileInfo удобно тем, что не нужно руками разделять "путь существует" и "это файл, а не каталог".
    const QFileInfo info(pluginFile);
    // Подходит только реально существующий обычный файл plugin-а.
    return info.exists() && info.isFile();
}

// ============================================================================
// ФУНКЦИЯ SplitPathList - РАЗБОР СПИСКА ПУТЕЙ ИЗ ПЕРЕМЕННОЙ ОКРУЖЕНИЯ
// ============================================================================
// Разбирает значение вида PATH-переменной в список отдельных каталогов.
// Учитывает системный разделитель путей и убирает пустые/мусорные элементы.
// Принимает:
// - rawValue: сырое значение переменной окружения в виде QByteArray.
// Возвращает:
// - список нормализованных путей.
// ============================================================================
QStringList SplitPathList(const QByteArray& rawValue)
{
    // Итоговый список нормализованных путей.
    QStringList paths;
    // Переводим сырое значение переменной окружения в QString текущей локали.
    // fromLocal8Bit здесь уместен, потому что переменные окружения приходят как байты ОС,
    // а не как заранее гарантированный UTF-8.
    const QString value = QString::fromLocal8Bit(rawValue);
    // Пустое значение означает, что путей для разбора нет.
    if (value.isEmpty())
    {
        return paths;
    }

    // Разбиваем строку по системному разделителю путей и сразу отбрасываем пустые части.
    // QDir::listSeparator() сам подставит ':' на Unix/macOS и ';' на Windows.
    const QStringList parts = value.split(QDir::listSeparator(), Qt::SkipEmptyParts);
    // Переменную цикла объявляем отдельно в "институтском" стиле, без for-range и auto.
    int i = 0;
    for (i = 0; i < parts.size(); ++i)
    {
        // Убираем лишние пробелы и нормализуем путь.
        // cleanPath не проверяет существование каталога, а только приводит строку пути к каноничному виду.
        const QString path = QDir::cleanPath(parts[i].trimmed());
        // В итоговый список попадают только непустые корректные элементы.
        if (!path.isEmpty())
        {
            // Оператор << у QStringList добавляет новый элемент в конец списка.
            paths << path;
        }
    }

    // Возвращаем уже очищенный список каталогов.
    return paths;
}

// ============================================================================
// ФУНКЦИЯ FindQtPlatformsPath - ПОИСК КАТАЛОГА QT PLATFORMS
// ============================================================================
// Ищет папку, в которой лежит plugin cocoa, используя несколько источников:
// 1) подсказку из CMake;
// 2) переменные окружения Qt;
// 3) стандартный путь Qt через QLibraryInfo;
// 4) установку qtbase через Homebrew.
// Принимает:
// - ничего.
// Возвращает:
// - путь до каталога platforms, если он найден;
// - пустую строку, если подходящий путь найти не удалось.
// ============================================================================
QString FindQtPlatformsPath()
{
    // Собираем все возможные каталоги-кандидаты, где может лежать папка platforms.
    QStringList candidates;

#ifdef LAB2_QT_PLATFORMS_HINT
    // Подсказка из CMake удобна, когда путь к plugin-ам известен еще на этапе конфигурации.
    const QString cmakeHint = QString::fromUtf8(LAB2_QT_PLATFORMS_HINT);
    if (!cmakeHint.isEmpty())
    {
        // Сначала кладем CMake-подсказку, потому что она обычно наиболее точная.
        candidates << QDir::cleanPath(cmakeHint);
    }
#endif

    // Приоритетно учитываем явно заданный путь к платформенным plugin-ам из окружения.
    // qgetenv(...) возвращает QByteArray, потому что на уровне ОС это просто набор байтов.
    const QString envPlatformPath = QString::fromLocal8Bit(qgetenv("QT_QPA_PLATFORM_PLUGIN_PATH"));
    if (!envPlatformPath.isEmpty())
    {
        candidates << QDir::cleanPath(envPlatformPath);
    }

    // Если задан общий путь QT_PLUGIN_PATH, дописываем к каждому корню подпапку platforms.
    const QStringList envPluginRoots = SplitPathList(qgetenv("QT_PLUGIN_PATH"));
    int i = 0;
    for (i = 0; i < envPluginRoots.size(); ++i)
    {
        // Из общего корня plugin-ов строим путь именно к подпапке platforms.
        candidates << QDir(envPluginRoots[i]).filePath("platforms");
    }

    // Берем стандартный путь plugin-ов самой установки Qt, если Qt его знает.
    // QLibraryInfo::path(...) запрашивает путь у самой Qt, а не у ОС, поэтому это
    // хороший fallback, когда окружение не настроено явно.
    const QString qlibPlugins = QLibraryInfo::path(QLibraryInfo::PluginsPath);
    if (!qlibPlugins.isEmpty())
    {
        candidates << QDir(qlibPlugins).filePath("platforms");
    }

    // На macOS с Homebrew дополнительно пробуем узнать корень qtbase через brew.
    // QProcess запускает внешнюю команду синхронно только на короткое время.
    QProcess brewProcess;
    // Используем start(program, arguments), чтобы не собирать shell-строку вручную.
    brewProcess.start(QString::fromUtf8("brew"),
                      QStringList() << QString::fromUtf8("--prefix") << QString::fromUtf8("qtbase"));
    // Если `brew --prefix qtbase` отработал успешно, строим еще один кандидатный путь.
    // waitForFinished(1500) ограничивает ожидание 1.5 секундами, чтобы старт приложения
    // не зависал надолго, если brew недоступен.
    if (brewProcess.waitForFinished(1500) && brewProcess.exitCode() == 0)
    {
        // readAllStandardOutput() читает весь stdout дочернего процесса; trim убирает '\n' в конце.
        const QString brewQtBase =
            QString::fromLocal8Bit(brewProcess.readAllStandardOutput()).trimmed();
        if (!brewQtBase.isEmpty())
        {
            candidates << QDir::cleanPath(QDir(brewQtBase).filePath("share/qt/plugins/platforms"));
        }
    }

    // Убираем пустые и повторяющиеся пути, чтобы не проверять один и тот же каталог много раз.
    QStringList uniqueCandidates;
    for (i = 0; i < candidates.size(); ++i)
    {
        const QString path = QDir::cleanPath(candidates[i]);
        // contains(...) здесь дает простую дедупликацию без дополнительного контейнера set.
        if (!path.isEmpty() && !uniqueCandidates.contains(path))
        {
            uniqueCandidates << path;
        }
    }

    // Ищем первый каталог, в котором реально присутствует plugin cocoa.
    for (i = 0; i < uniqueCandidates.size(); ++i)
    {
        // Как только нашли рабочий путь, сразу возвращаем его; остальные кандидаты уже не нужны.
        if (HasCocoaPlugin(uniqueCandidates[i]))
        {
            return uniqueCandidates[i];
        }
    }

    // Если ни один путь не подошел, возвращаем пустую строку.
    return QString();
}

// ============================================================================
// ФУНКЦИЯ PrepareQtRuntimePlugins - ПОДГОТОВКА ПУТЕЙ К QT-PLUGIN-АМ
// ============================================================================
// На macOS помогает приложению найти платформенный plugin cocoa до создания
// QApplication.
// Принимает:
// - ничего.
// Возвращает:
// - ничего; при успехе настраивает переменные окружения процесса.
// ============================================================================
void PrepareQtRuntimePlugins()
{
#if defined(__APPLE__)
    // Этот блок нужен только на macOS; на других платформах plugin cocoa не используется.
    // На macOS пытаемся заранее найти каталог platforms, чтобы Qt не падал на старте GUI.
    const QString platformsPath = FindQtPlatformsPath();
    if (!platformsPath.isEmpty())
    {
        // Из папки platforms поднимаемся на уровень выше, потому что QT_PLUGIN_PATH
        // должен указывать на общий корень plugin-ов, а не на сам каталог platforms.
        // cdUp() меняет сам объект QDir, а не возвращает новый путь, поэтому далее
        // absolutePath() вызывается уже на модифицированном каталоге.
        QDir pluginsDir(platformsPath);
        pluginsDir.cdUp();
        const QString pluginsPath = pluginsDir.absolutePath();

        // Сообщаем Qt:
        // - общий каталог plugin-ов;
        // - точный каталог platform plugin-ов;
        // - требуемую платформу cocoa.
        qputenv("QT_PLUGIN_PATH", pluginsPath.toUtf8());
        qputenv("QT_QPA_PLATFORM_PLUGIN_PATH", platformsPath.toUtf8());
        qputenv("QT_QPA_PLATFORM", "cocoa");
    }
#endif
}

}  // namespace

// ============================================================================
// ФУНКЦИЯ main - ЗАПУСК ПРИЛОЖЕНИЯ LAB_2
// ============================================================================
// Шаги:
// 1) готовим пути к Qt-plugin-ам;
// 2) создаем QApplication;
// 3) создаем оптимизатор и парсер;
// 4) создаем и показываем главное окно;
// 5) передаем управление в цикл событий Qt.
// Принимает:
// - argc, argv: стандартные аргументы командной строки.
// Возвращает:
// - код завершения приложения из цикла событий Qt.
// ============================================================================
int main(int argc, char* argv[])
{
    // Подготавливаем runtime-пути к платформенным plugin-ам Qt.
    // Это делаем до QApplication, потому что после создания app часть настроек Qt уже зафиксирована.
    PrepareQtRuntimePlugins();

    // Создаем основной объект Qt-приложения.
    // argc/argv передаются в QApplication, чтобы Qt мог обработать свои ключи командной строки.
    QApplication app(argc, argv);

    // Создаем объект метода Розенброка с дискретным шагом.
    // Объект живет на стеке main и существует весь срок жизни главного окна.
    const lab2::RosenbrockDiscreteOptimizer optimizer;
    // Создаем парсер пользовательской функции через muparser.
    // const здесь означает, что после создания эти сервисы не должны менять свое состояние снаружи main.
    const lab2::MuParserObjectiveParser parser;

    // Создаем и показываем главное окно GUI.
    // Окно получает ссылки на optimizer и parser, поэтому они должны жить дольше самого window.
    lab2::MainWindow window(optimizer, parser);
    window.show();

    // Запускаем цикл обработки событий Qt и возвращаем код завершения.
    // exec() блокирует поток до закрытия приложения, поэтому эта строка естественно последняя в main.
    return app.exec();
}
