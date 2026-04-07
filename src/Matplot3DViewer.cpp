// ============================================================================
// ФАЙЛ MATPLOT3DVIEWER.CPP
// ============================================================================
// Назначение:
// Реализовать отдельное 3D-окно визуализации метода Розенброка на базе
// Qt DataVisualization.
//
// Что содержит файл:
// 1) вспомогательные структуры и функции для подготовки 3D-данных;
// 2) виджет Rosenbrock3DViewerWidget с панелью управления и графиком;
// 3) публичную точку входа ShowRosenbrock3DViewer(...);
// 4) fallback-ветку на случай сборки без Qt DataVisualization.
// ============================================================================

// Публичное объявление интерфейса 3D-вьювера.
#include "lab2/Matplot3DViewer.h"

#if LAB2_HAS_QTDATAVIS3D

// Базовые QtCore-типы для путей, окружения, процессов и таймеров.
#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QEventLoop>
#include <QtCore/QFileInfo>
#include <QtCore/QLibraryInfo>
#include <QtCore/QObject>
#include <QtCore/QProcess>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QTimer>
#include <QtCore/Qt>

// Классы Qt DataVisualization для 3D-scatter графика.
#include <QtDataVisualization/Q3DCamera>
#include <QtDataVisualization/Q3DScatter>
#include <QtDataVisualization/Q3DScene>
#include <QtDataVisualization/Q3DTheme>
#include <QtDataVisualization/QAbstract3DGraph>
#include <QtDataVisualization/QScatter3DSeries>
#include <QtDataVisualization/QScatterDataItem>
#include <QtDataVisualization/QScatterDataProxy>
#include <QtDataVisualization/QValue3DAxis>

// Графические и виджетные классы обычного Qt Widgets UI.
#include <QtGui/QColor>
#include <QtGui/QResizeEvent>
#include <QtGui/QVector3D>
#include <QtWidgets/QApplication>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QFrame>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSlider>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

// Стандартная библиотека для численных и строковых helper-функций.
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace lab2
{

// ----------------------------------------------------------------------------
// СТРУКТУРА STEPVISUALSTATE3D
// ----------------------------------------------------------------------------
// Назначение:
// Хранить полностью подготовленное состояние текущего шага для отрисовки:
// путь, текущую точку, неудачные попытки и служебную статистику.
// ----------------------------------------------------------------------------
struct StepVisualState3D
{
    // acceptedPath содержит уже принятые точки траектории метода.
    std::vector<Vector> acceptedPath;

    // failedPoints накапливает неудачные пробные точки текущей серии.
    std::vector<Vector> failedPoints;

    // currentPoint — точка, которую надо выделять как текущую.
    Vector currentPoint;

    // hasCurrentPoint показывает, что currentPoint действительно заполнена.
    bool hasCurrentPoint;

    // hasRow показывает, есть ли уже реальная строка трассировки для шага.
    bool hasRow;

    // k — номер итерации метода Розенброка.
    std::size_t k;

    // j — номер внутреннего шага по направлению.
    std::size_t j;

    // delta — текущее значение длины шага вдоль направления.
    double delta;

    // successfulStep показывает, оказался ли последний шаг удачным.
    bool successfulStep;

    // rollback сигнализирует об откате на текущем проходе.
    bool rollback;

    // directionChanged показывает, была ли перестройка базиса.
    bool directionChanged;

    // successfulCount — число удачных шагов, уже попавших в показанный префикс.
    std::size_t successfulCount;

    // failedCount — число неудачных шагов, уже попавших в показанный префикс.
    std::size_t failedCount;

    // Конструктор по умолчанию сразу переводит структуру в пустое состояние.
    StepVisualState3D()
        : hasCurrentPoint(false),
          hasRow(false),
          k(0),
          j(0),
          delta(0.0),
          successfulStep(false),
          rollback(false),
          directionChanged(false),
          successfulCount(0),
          failedCount(0)
    {
    }
};

// ----------------------------------------------------------------------------
// СТРУКТУРА BOUNDS3D
// ----------------------------------------------------------------------------
// Назначение:
// Хранить 3D-границы по трем осям для дальнейшей настройки осей графика.
// ----------------------------------------------------------------------------
struct Bounds3D
{
    // Минимум по оси X.
    double xMin;

    // Максимум по оси X.
    double xMax;

    // Минимум по оси Y.
    double yMin;

    // Максимум по оси Y.
    double yMax;

    // Минимум по оси Z.
    double zMin;

    // Максимум по оси Z.
    double zMax;

    // hasData показывает, удалось ли уже добавить в bounds хоть одну точку.
    bool hasData;

    // Инициализируем границы бесконечностями, чтобы потом их было удобно
    // сужать/расширять обычными min/max-проверками.
    Bounds3D()
        : xMin(std::numeric_limits<double>::infinity()),
          xMax(-std::numeric_limits<double>::infinity()),
          yMin(std::numeric_limits<double>::infinity()),
          yMax(-std::numeric_limits<double>::infinity()),
          zMin(std::numeric_limits<double>::infinity()),
          zMax(-std::numeric_limits<double>::infinity()),
          hasData(false)
    {
    }
};

// ----------------------------------------------------------------------------
// ФУНКЦИЯ FORMATVECTOR3
// ----------------------------------------------------------------------------
// Назначение:
// Преобразовать вектор координат в компактную строку для нижней статусной
// панели 3D-вьювера.
// ----------------------------------------------------------------------------
static std::string FormatVector3(const Vector& values)
{
    // ostringstream удобен тем, что позволяет собирать строку через << в том
    // же стиле, что и обычный потоковый вывод C++.
    std::ostringstream out;

    // Открывающая скобка визуально обозначает начало вектора.
    out << "(";
    for (std::size_t i = 0; i < values.size(); ++i)
    {
        // Разделитель добавляем только начиная со второго элемента.
        if (i > 0)
        {
            out << ", ";
        }

        // std::fixed и setprecision(4) фиксируют одинаковый формат для всех
        // координат в подписи.
        out << std::fixed << std::setprecision(4) << values[i];
    }

    // Закрывающая скобка завершает текстовое представление вектора.
    out << ")";

    // str() возвращает накопленную std::string из потока.
    return out.str();
}

// ----------------------------------------------------------------------------
// ФУНКЦИЯ BUILDSTATEUNTILSTEP3D
// ----------------------------------------------------------------------------
// Назначение:
// Построить укороченное визуальное состояние траектории на префиксе шагов
// от начала до shownStep включительно.
// ----------------------------------------------------------------------------
static void BuildStateUntilStep3D(const OptimizationResult& result,
                                  std::size_t shownStep,
                                  StepVisualState3D& out)
{
    // Сначала полностью сбрасываем выходную структуру, чтобы не тянуть
    // значения от предыдущего кадра.
    out = StepVisualState3D();

    // Если трассировка пуста, строить визуальное состояние не из чего.
    if (result.trace.empty())
    {
        return;
    }

    // Начальной текущей точкой считаем базисную точку первой строки трассы.
    Vector current = result.trace[0].yi;

    // Для 3D-визуализации нужны как минимум три координаты пространства
    // переменных.
    if (current.size() < 3)
    {
        return;
    }

    // В начале показанного пути всегда лежит стартовая допустимая точка.
    out.acceptedPath.push_back(current);

    // currentPoint нужен для отдельного выделения крупным маркером.
    out.currentPoint = current;

    // Отмечаем, что текущая точка уже валидна.
    out.hasCurrentPoint = true;

    // Если GUI попросил шаг за пределом трассы, аккуратно прижимаем его к
    // последнему реально существующему шагу.
    if (shownStep > result.trace.size())
    {
        shownStep = result.trace.size();
    }

    // Внешний индекс объявляем заранее в принятом в проекте стиле.
    std::size_t i = 0;
    for (i = 0; i < shownStep; ++i)
    {
        // row — текущая строка трассировки, которую переносим в визуальное
        // состояние.
        const OptimizationResult::TraceRow& row = result.trace[i];

        // Если шаг был удачным, он реально двигает метод вперед по траектории.
        if (row.successfulStep)
        {
            // Увеличиваем счетчик удачных шагов для нижней панели.
            ++out.successfulCount;

            // После удачного шага ранее накопленные неудачные точки уже не
            // относятся к текущей серии попыток.
            out.failedPoints.clear();

            // Для показа новой текущей точки trialPoint должна быть полной
            // 3-мерной координатой.
            if (row.trialPoint.size() >= 3)
            {
                // current теперь переходит в новую принятую точку.
                current = row.trialPoint;

                // Добавляем новую принятую точку в полилинию пути.
                out.acceptedPath.push_back(current);

                // Параллельно обновляем крупный текущий маркер.
                out.currentPoint = current;
                out.hasCurrentPoint = true;
            }
        }
        else
        {
            // Неудачный шаг увеличивает только счетчик промахов.
            ++out.failedCount;

            // Неудачные пробные точки сохраняем отдельно красными маркерами.
            if (row.trialPoint.size() >= 3)
            {
                out.failedPoints.push_back(row.trialPoint);
            }
        }
    }

    // Если показан хотя бы один шаг, нижняя панель должна отражать
    // параметры именно последней просмотренной строки.
    if (shownStep > 0)
    {
        // last — последняя строка, вошедшая в текущий префикс трассы.
        const OptimizationResult::TraceRow& last = result.trace[shownStep - 1];

        // hasRow сообщает, что далее можно безопасно использовать k/j/delta.
        out.hasRow = true;
        out.k = last.k;
        out.j = last.j;
        out.delta = last.delta;
        out.successfulStep = last.successfulStep;
        out.rollback = last.rollback;
        out.directionChanged = last.directionChanged;
    }
    else
    {
        // При shownStep == 0 метод еще фактически стоит в начальной точке.
        out.hasRow = false;

        // k берем из первой строки трассы, потому что она знает реальный
        // номер первой итерации метода.
        out.k = result.trace[0].k;

        // j=0 используется здесь как специальная метка "до первого шага".
        out.j = 0;
    }
}

// ----------------------------------------------------------------------------
// ФУНКЦИЯ MAXITERATIONK
// ----------------------------------------------------------------------------
// Назначение:
// Найти максимальный номер итерации k в трассировке.
// ----------------------------------------------------------------------------
static std::size_t MaxIterationK(const OptimizationResult& result)
{
    // Начинаем с 1, потому что в методе Розенброка итерации нумеруются с 1.
    std::size_t maxK = 1;

    // Внешний индекс объявляем заранее в стиле остальных файлов проекта.
    std::size_t i = 0;
    for (i = 0; i < result.trace.size(); ++i)
    {
        // Если в очередной строке найден больший k, обновляем максимум.
        if (result.trace[i].k > maxK)
        {
            maxK = result.trace[i].k;
        }
    }

    // Возвращаем финальный максимум для строки состояния вьювера.
    return maxK;
}

// ----------------------------------------------------------------------------
// ФУНКЦИЯ VARIABLENAME
// ----------------------------------------------------------------------------
// Назначение:
// Построить имя координаты x1, x2, x3 по нулевому индексу.
// ----------------------------------------------------------------------------
static QString VariableName(std::size_t index)
{
    // index + 1 нужен, потому что в математической записи переменные
    // нумеруются с единицы, а не с нуля.
    return QString::fromUtf8("x") + QString::number(static_cast<unsigned long long>(index + 1));
}

// ----------------------------------------------------------------------------
// ФУНКЦИЯ FORMATBASISTEXT
// ----------------------------------------------------------------------------
// Назначение:
// Преобразовать набор направлений базиса в строку вида
// `s1=(...), s2=(...), s3=(...)`.
// ----------------------------------------------------------------------------
static std::string FormatBasisText(const std::vector<Vector>& basis)
{
    // Пустой базис визуально отображаем дефисом, а не пустой строкой.
    if (basis.empty())
    {
        return "-";
    }

    // Поток используется для пошаговой сборки длинного текста базиса.
    std::ostringstream out;

    // Внешний индекс объявляем отдельно, чтобы код оставался однородным с
    // остальными файлами проекта.
    std::size_t i = 0;
    for (i = 0; i < basis.size(); ++i)
    {
        // Между направлениями вставляем разделитель `; `.
        if (i > 0)
        {
            out << "; ";
        }

        // Сборка подписи `s_j=(...)` делается через уже готовый FormatVector3.
        out << "s" << (i + 1) << "=" << FormatVector3(basis[i]);
    }

    // Возвращаем собранное текстовое описание базиса.
    return out.str();
}

static void UpdateBoundsFromPoint(Bounds3D& bounds,
                                  const Vector& point,
                                  double fValue,
                                  std::size_t axisA,
                                  std::size_t axisB)
{
    // Если у точки не хватает нужных координат или значение функции нечисловое,
    // обновлять границы нельзя.
    if (point.size() <= axisA || point.size() <= axisB || !std::isfinite(fValue))
    {
        return;
    }

    // В проекции вида (x_axisA, f(x), x_axisB) ось Y занята значением функции.
    const double x = point[axisA];
    const double y = fValue;
    const double z = point[axisB];

    // Дополнительно защищаемся от NaN/inf уже после извлечения координат.
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
    {
        return;
    }

    // Каждая из шести проверок независимо обновляет границы прямоугольного
    // параллелепипеда, охватывающего все точки.
    if (x < bounds.xMin)
    {
        bounds.xMin = x;
    }
    if (x > bounds.xMax)
    {
        bounds.xMax = x;
    }
    if (y < bounds.yMin)
    {
        bounds.yMin = y;
    }
    if (y > bounds.yMax)
    {
        bounds.yMax = y;
    }
    if (z < bounds.zMin)
    {
        bounds.zMin = z;
    }
    if (z > bounds.zMax)
    {
        bounds.zMax = z;
    }

    // После первой же успешной точки bounds становится осмысленным.
    bounds.hasData = true;
}

// ----------------------------------------------------------------------------
// ФУНКЦИЯ UPDATEBOUNDSFROMPOINTVARIABLES
// ----------------------------------------------------------------------------
// Назначение:
// Обновить границы для режима показа обычного пространства переменных
// `x1, x2, x3`, где все три оси заняты самими координатами.
// ----------------------------------------------------------------------------
static void UpdateBoundsFromPointVariables(Bounds3D& bounds, const Vector& point)
{
    // В пространстве переменных 3D-точка должна иметь хотя бы три координаты.
    if (point.size() < 3)
    {
        return;
    }

    // Для обычного пространства переменных координаты берутся напрямую.
    const double x = point[0];
    const double y = point[1];
    const double z = point[2];

    // Неконечные значения нельзя использовать для настройки осей графика.
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
    {
        return;
    }

    // Дальше логика полностью аналогична UpdateBoundsFromPoint(...), только
    // все оси заняты координатами переменных.
    if (x < bounds.xMin)
    {
        bounds.xMin = x;
    }
    if (x > bounds.xMax)
    {
        bounds.xMax = x;
    }
    if (y < bounds.yMin)
    {
        bounds.yMin = y;
    }
    if (y > bounds.yMax)
    {
        bounds.yMax = y;
    }
    if (z < bounds.zMin)
    {
        bounds.zMin = z;
    }
    if (z > bounds.zMax)
    {
        bounds.zMax = z;
    }

    // Отмечаем, что bounds уже был заполнен хотя бы одной корректной точкой.
    bounds.hasData = true;
}

// ----------------------------------------------------------------------------
// ФУНКЦИЯ BUILDBOUNDSFORPROJECTION
// ----------------------------------------------------------------------------
// Назначение:
// Построить итоговые границы для проекции вида `(x_axisA, f(x), x_axisB)`.
// ----------------------------------------------------------------------------
static Bounds3D BuildBoundsForProjection(const OptimizationResult& result,
                                         std::size_t axisA,
                                         std::size_t axisB)
{
    // bounds будет последовательно расширяться всеми точками трассы.
    Bounds3D bounds;

    // Внешний индекс объявляем заранее в стиле остальных файлов проекта.
    std::size_t i = 0;
    for (i = 0; i < result.trace.size(); ++i)
    {
        // row — очередная строка трассировки оптимизации.
        const OptimizationResult::TraceRow& row = result.trace[i];

        // Учитываем и базисную точку итерации, и промежуточную y_i, и пробную
        // точку шага, потому что все они потенциально видимы на графике.
        UpdateBoundsFromPoint(bounds, row.xk, row.fxk, axisA, axisB);
        UpdateBoundsFromPoint(bounds, row.yi, row.fyi, axisA, axisB);
        UpdateBoundsFromPoint(bounds, row.trialPoint, row.fTrial, axisA, axisB);
    }

    // Отдельно учитываем итоговый оптимум, даже если он не совпадает с
    // последней trialPoint в трассировке.
    UpdateBoundsFromPoint(bounds, result.optimumX, result.optimumValue, axisA, axisB);

    // Если по какой-то причине ни одной валидной точки не нашлось, отдаем
    // безопасный стандартный куб [-1, 1]^3.
    if (!bounds.hasData)
    {
        bounds.xMin = -1.0;
        bounds.xMax = 1.0;
        bounds.yMin = -1.0;
        bounds.yMax = 1.0;
        bounds.zMin = -1.0;
        bounds.zMax = 1.0;
        return bounds;
    }

    // dx, dy, dz — реальные размеры сцены по трем осям до добавления полей.
    double dx = bounds.xMax - bounds.xMin;
    double dy = bounds.yMax - bounds.yMin;
    double dz = bounds.zMax - bounds.zMin;

    // Если диапазон по оси выродился в точку, искусственно расширяем его,
    // иначе ось будет плохо отображаться.
    if (!(dx > 0.0))
    {
        dx = 1.0;
        bounds.xMin -= 0.5;
        bounds.xMax += 0.5;
    }
    if (!(dy > 0.0))
    {
        dy = 1.0;
        bounds.yMin -= 0.5;
        bounds.yMax += 0.5;
    }
    if (!(dz > 0.0))
    {
        dz = 1.0;
        bounds.zMin -= 0.5;
        bounds.zMax += 0.5;
    }

    // Поля вокруг данных нужны, чтобы точки и подписи не липли к краям осей.
    const double padX = std::max(0.12, dx * 0.18);
    const double padY = std::max(0.12, dy * 0.18);
    const double padZ = std::max(0.12, dz * 0.18);

    // Расширяем диапазон симметрично в обе стороны по каждой оси.
    bounds.xMin -= padX;
    bounds.xMax += padX;
    bounds.yMin -= padY;
    bounds.yMax += padY;
    bounds.zMin -= padZ;
    bounds.zMax += padZ;

    // Возвращаем уже готовые границы для настройки осей Q3DScatter.
    return bounds;
}

// ----------------------------------------------------------------------------
// ФУНКЦИЯ BUILDBOUNDSFORVARIABLESPACE
// ----------------------------------------------------------------------------
// Назначение:
// Построить границы для режима отображения `(x1, x2, x3)` без оси функции.
// ----------------------------------------------------------------------------
static Bounds3D BuildBoundsForVariableSpace(const OptimizationResult& result)
{
    // bounds будет охватывать все реальные точки в пространстве переменных.
    Bounds3D bounds;

    std::size_t i = 0;
    for (i = 0; i < result.trace.size(); ++i)
    {
        const OptimizationResult::TraceRow& row = result.trace[i];

        // В пространстве переменных учитываем все точки, которые реально
        // появлялись в процессе движения метода.
        UpdateBoundsFromPointVariables(bounds, row.xk);
        UpdateBoundsFromPointVariables(bounds, row.yi);
        UpdateBoundsFromPointVariables(bounds, row.trialPoint);
    }

    // Оптимум тоже включаем в итоговый охватывающий бокс.
    UpdateBoundsFromPointVariables(bounds, result.optimumX);

    // Fallback-диапазон нужен, если все точки по какой-то причине невалидны.
    if (!bounds.hasData)
    {
        bounds.xMin = -1.0;
        bounds.xMax = 1.0;
        bounds.yMin = -1.0;
        bounds.yMax = 1.0;
        bounds.zMin = -1.0;
        bounds.zMax = 1.0;
        return bounds;
    }

    // Реальные размеры данных по трем координатным осям.
    double dx = bounds.xMax - bounds.xMin;
    double dy = bounds.yMax - bounds.yMin;
    double dz = bounds.zMax - bounds.zMin;

    // Вырожденный диапазон оси принудительно расширяем.
    if (!(dx > 0.0))
    {
        dx = 1.0;
        bounds.xMin -= 0.5;
        bounds.xMax += 0.5;
    }
    if (!(dy > 0.0))
    {
        dy = 1.0;
        bounds.yMin -= 0.5;
        bounds.yMax += 0.5;
    }
    if (!(dz > 0.0))
    {
        dz = 1.0;
        bounds.zMin -= 0.5;
        bounds.zMax += 0.5;
    }

    // Поля позволяют сцене выглядеть менее зажатой у краев.
    const double padX = std::max(0.12, dx * 0.18);
    const double padY = std::max(0.12, dy * 0.18);
    const double padZ = std::max(0.12, dz * 0.18);

    bounds.xMin -= padX;
    bounds.xMax += padX;
    bounds.yMin -= padY;
    bounds.yMax += padY;
    bounds.zMin -= padZ;
    bounds.zMax += padZ;

    return bounds;
}

// ----------------------------------------------------------------------------
// ФУНКЦИЯ HASCOCOAPLUGIN
// ----------------------------------------------------------------------------
// Назначение:
// Проверить, содержит ли каталог platforms нужный macOS-плагин libqcocoa.
// ----------------------------------------------------------------------------
static bool HasCocoaPlugin(const QString& platformsPath)
{
    // Пустой путь заранее считаем невалидным.
    if (platformsPath.isEmpty())
    {
        return false;
    }

    // filePath(...) аккуратно склеивает каталог и имя файла платформенного
    // плагина.
    const QString pluginFile = QDir(platformsPath).filePath("libqcocoa.dylib");

    // QFileInfo позволяет проверить существование и тип файла без открытия.
    const QFileInfo info(pluginFile);

    // Для запуска нужен именно существующий обычный файл плагина.
    return info.exists() && info.isFile();
}

// ----------------------------------------------------------------------------
// ФУНКЦИЯ SPLITPATHLIST
// ----------------------------------------------------------------------------
// Назначение:
// Разобрать переменную окружения со списком путей через системный разделитель.
// ----------------------------------------------------------------------------
static QStringList SplitPathList(const QByteArray& rawValue)
{
    // Итоговый список путей собираем постепенно.
    QStringList paths;

    // fromLocal8Bit корректно интерпретирует байты переменной окружения как
    // локальную системную кодировку.
    const QString value = QString::fromLocal8Bit(rawValue);

    // Пустую переменную окружения раскладывать не нужно.
    if (value.isEmpty())
    {
        return paths;
    }

    // QDir::listSeparator() возвращает системный разделитель списков путей:
    // `:` на Unix/macOS и `;` на Windows.
    const QStringList parts = value.split(QDir::listSeparator(), Qt::SkipEmptyParts);

    // Индекс объявляем отдельно в учебном стиле, принятом в проекте.
    int i = 0;
    for (i = 0; i < parts.size(); ++i)
    {
        // trimmed() убирает внешние пробелы, cleanPath() нормализует путь.
        const QString path = QDir::cleanPath(parts[i].trimmed());

        // В итоговый список попадают только непустые кандидаты.
        if (!path.isEmpty())
        {
            paths.push_back(path);
        }
    }

    return paths;
}

// ----------------------------------------------------------------------------
// ФУНКЦИЯ FINDQTPLATFORMSPATH
// ----------------------------------------------------------------------------
// Назначение:
// Найти каталог Qt `platforms`, содержащий плагин `libqcocoa.dylib`.
// ----------------------------------------------------------------------------
static QString FindQtPlatformsPath()
{
    // candidates накапливает все потенциальные каталоги platforms.
    QStringList candidates;

#ifdef LAB2_QT_PLATFORMS_HINT
    // CMake может заранее подсказать вероятный каталог с Qt platform plugins.
    const QString cmakeHint = QString::fromUtf8(LAB2_QT_PLATFORMS_HINT);
    if (!cmakeHint.isEmpty())
    {
        candidates.push_back(QDir::cleanPath(cmakeHint));
    }
#endif

    // Явный путь к platform plugins из окружения имеет высокий приоритет.
    const QString envPath = QString::fromLocal8Bit(qgetenv("QT_QPA_PLATFORM_PLUGIN_PATH"));
    if (!envPath.isEmpty())
    {
        candidates.push_back(QDir::cleanPath(envPath));
    }

    // QT_PLUGIN_PATH обычно хранит корни плагинов, поэтому добавляем к ним
    // подпапку `platforms`.
    const QStringList envPluginRoots = SplitPathList(qgetenv("QT_PLUGIN_PATH"));
    int i = 0;
    for (i = 0; i < envPluginRoots.size(); ++i)
    {
        candidates.push_back(QDir(envPluginRoots[i]).filePath("platforms"));
    }

    // QLibraryInfo знает путь к плагинам для текущей Qt-сборки.
    const QString qlibPlugins = QLibraryInfo::path(QLibraryInfo::PluginsPath);
    if (!qlibPlugins.isEmpty())
    {
        candidates.push_back(QDir(qlibPlugins).filePath("platforms"));
    }

    // Дополнительно пытаемся спросить Homebrew, где лежит qtbase.
    QProcess brewProcess;
    brewProcess.start(QString::fromUtf8("brew"), QStringList() << QString::fromUtf8("--prefix")
                                                               << QString::fromUtf8("qtbase"));
    if (brewProcess.waitForFinished(1500) && brewProcess.exitCode() == 0)
    {
        // Из стандартного вывода brew получаем корень установки qtbase.
        const QString brewQtBase =
            QString::fromLocal8Bit(brewProcess.readAllStandardOutput()).trimmed();
        if (!brewQtBase.isEmpty())
        {
            candidates.push_back(QDir::cleanPath(QDir(brewQtBase).filePath("share/qt/plugins/platforms")));
        }
    }

    // Отдельно проверяем стандартную схему app bundle, когда плагины лежат
    // рядом с приложением в `../plugins/platforms`.
    const QString appPluginsPath = QDir(QCoreApplication::applicationDirPath()).filePath("../plugins/platforms");
    if (!appPluginsPath.isEmpty())
    {
        candidates.push_back(QDir::cleanPath(appPluginsPath));
    }

    // uniqueCandidates убирает дубликаты, чтобы не проверять один и тот же
    // путь несколько раз.
    QStringList uniqueCandidates;
    for (i = 0; i < candidates.size(); ++i)
    {
        const QString path = QDir::cleanPath(candidates[i]);
        if (!path.isEmpty() && !uniqueCandidates.contains(path))
        {
            uniqueCandidates.push_back(path);
        }
    }

    // Возвращаем первый каталог, в котором действительно найден нужный
    // macOS-platform plugin.
    for (i = 0; i < uniqueCandidates.size(); ++i)
    {
        if (HasCocoaPlugin(uniqueCandidates[i]))
        {
            return uniqueCandidates[i];
        }
    }

    // Если ничего не найдено, вызывающий код сам сформирует сообщение об ошибке.
    return QString();
}

// ----------------------------------------------------------------------------
// ФУНКЦИЯ PREPAREQTRUNTIMEPLUGINS
// ----------------------------------------------------------------------------
// Назначение:
// Подготовить переменные окружения Qt plugins перед запуском 3D-вьювера.
// ----------------------------------------------------------------------------
static bool PrepareQtRuntimePlugins(std::string& error)
{
#if defined(__APPLE__)
    // На macOS обязательно нужен путь к каталогу platforms с libqcocoa.
    const QString platformsPath = FindQtPlatformsPath();
    if (platformsPath.isEmpty())
    {
        error = "Не найден Qt platform plugin libqcocoa.dylib. Проверьте установку qtbase.";
        return false;
    }

    // Поднимаемся на уровень выше, потому что QT_PLUGIN_PATH должен указывать
    // на корень plugins, а не на подпапку platforms.
    QDir pluginsDir(platformsPath);
    pluginsDir.cdUp();
    const QString pluginsPath = pluginsDir.absolutePath();

    // qputenv(...) меняет окружение текущего процесса до создания QApplication.
    qputenv("QT_PLUGIN_PATH", pluginsPath.toUtf8());
    qputenv("QT_QPA_PLATFORM_PLUGIN_PATH", platformsPath.toUtf8());
    qputenv("QT_QPA_PLATFORM", "cocoa");
#endif

    // На не-macOS дополнительная подготовка не требуется.
    return true;
}

// ----------------------------------------------------------------------------
// КЛАСС ROSENBROCK3DVIEWERWIDGET
// ----------------------------------------------------------------------------
// Назначение:
// Реализовать самостоятельное окно 3D-визуализации с графиком, таймлайном,
// управлением камерой и синхронизацией с таблицей шагов.
// ----------------------------------------------------------------------------
class Rosenbrock3DViewerWidget : public QWidget
{
public:
    // ------------------------------------------------------------------------
    // КОНСТРУКТОР ROSENBROCK3DVIEWERWIDGET
    // ------------------------------------------------------------------------
    // Назначение:
    // Собрать весь интерфейс 3D-вьювера, создать серии данных и подготовить
    // сцену к первой отрисовке.
    // ------------------------------------------------------------------------
    Rosenbrock3DViewerWidget(const IObjectiveFunction* function,
                             const OptimizationResult& result,
                             std::size_t kMax,
                             std::size_t initialStep,
                             const ViewerStepChangedCallback& onStepChanged,
                             QWidget* parent)
        : QWidget(parent),
          // function_ хранит исходную функцию для вычисления f(x) в нужных
          // точках при построении поверхности и статуса.
          function_(function),
          // result_ хранит полную трассировку метода, уже вычисленную заранее.
          result_(result),
          // kMax_ нужен для строки состояния "текущий k / максимальный k".
          kMax_(kMax),
          // stepMax_ равен числу строк трассировки, то есть числу шагов,
          // которые можно листать ползунком.
          stepMax_(result.trace.size()),
          // stepShown_ выбирает стартовый кадр анимации; если внешний код
          // передал `max()` или слишком большой индекс, стартуем с конца.
          stepShown_((initialStep == std::numeric_limits<std::size_t>::max() ||
                      initialStep > result.trace.size())
                         ? result.trace.size()
                         : initialStep),
          // По умолчанию открываемся в пространстве переменных x1, x2, x3.
          axisA_(0),
          axisB_(1),
          useFunctionProjection_(false),
          // До полной инициализации виджет считается неготовым.
          ready_(false),
          // onStepChanged_ нужен для подсветки строки таблицы в главном окне.
          onStepChanged_(onStepChanged)
    {
        // Заголовок окна сразу сообщает, что используется именно 3D-режим.
        setWindowTitle(QString::fromUtf8("Rosenbrock 3D Viewer (Qt DataVisualization)"));

        // Нижний предел размера не дает интерфейсу схлопнуться до состояния,
        // в котором элементы управления станут нечитабельными.
        setMinimumSize(1220, 800);

        // StrongFocus позволяет получать фокус клавиатуры при кликах и
        // корректно работать как активное самостоятельное окно.
        setFocusPolicy(Qt::StrongFocus);

        // objectName нужен для адресного применения QSS-стилей к корню виджета.
        setObjectName("viewerRoot");

        // setStyleSheet(...) локально задает внешний вид именно этому окну и
        // его дочерним элементам, не меняя тему всего приложения.
        setStyleSheet(
            "QWidget#viewerRoot { background: #101621; color: #e5e7eb; }"
            "QWidget#viewerTopPanel, QWidget#viewerBottomPanel {"
            "  background: #131c2a;"
            "  border: 1px solid #2f3c50;"
            "  border-radius: 12px;"
            "}"
            "QWidget#viewerGroup { background: transparent; border: 0; }"
            "QLabel#panelTitle { color: #9fb2cc; font-size: 13px; font-weight: 600; }"
            "QLabel#iterLabel { color: #f8fafc; font-size: 14px; font-weight: 700; padding: 0 3px; }"
            "QLabel#speedValueLabel { color: #f8fafc; font-size: 14px; font-weight: 700; min-width: 36px; }"
            "QLabel#infoLabel { color: #d3deee; font-size: 13px; padding-top: 0px; }"
            "QFrame#lineSep { background: #334155; min-width: 1px; max-width: 1px; }"
            "QComboBox {"
            "  background: #0f1827;"
            "  border: 1px solid #394960;"
            "  border-radius: 9px;"
            "  color: #f1f5f9;"
            "  padding: 3px 7px;"
            "  min-height: 16px;"
            "  font-size: 14px;"
            "}"
            "QComboBox:hover { border-color: #4f6a8f; }"
            "QComboBox::drop-down { border: 0; width: 14px; }"
            "QPushButton {"
            "  background: #1f2937;"
            "  border: 1px solid #3b4b63;"
            "  border-radius: 7px;"
            "  color: #e2e8f0;"
            "  font-size: 14px;"
            "  font-weight: 600;"
            "  min-height: 27px;"
            "  padding: 0 8px;"
            "}"
            "QPushButton:hover { background: #273447; border-color: #4d6485; }"
            "QPushButton:pressed { background: #1a2433; }"
            "QPushButton:disabled {"
            "  color: #6b7d96;"
            "  background: #1a2230;"
            "  border-color: #2f3a4b;"
            "}"
            "QPushButton[controlRole=\"icon\"] {"
            "  min-width: 30px; max-width: 30px;"
            "  min-height: 27px; max-height: 27px;"
            "  padding: 0px;"
            "  font-size: 17px;"
            "  font-weight: 700;"
            "}"
            "QPushButton[controlRole=\"nav\"] {"
            "  min-width: 34px; max-width: 34px;"
            "  min-height: 27px; max-height: 27px;"
            "  padding: 0px;"
            "}"
            "QPushButton[controlRole=\"accent\"] {"
            "  background: #2055cc;"
            "  border-color: #2f66e6;"
            "  color: #ffffff;"
            "  min-width: 32px; max-width: 32px;"
            "  min-height: 27px; max-height: 27px;"
            "  padding: 0px;"
            "}"
            "QPushButton[controlRole=\"accent\"]:hover { background: #2a63df; }"
            "QPushButton[controlRole=\"danger\"] {"
            "  background: #3a1f26;"
            "  border-color: #65404a;"
            "  color: #f9d6dc;"
            "  padding: 0 14px;"
            "}"
            "QPushButton[controlRole=\"danger\"]:hover { background: #4a2831; }"
            "QSlider::groove:horizontal {"
            "  border: 0;"
            "  background: #2d3748;"
            "  height: 5px;"
            "  border-radius: 4px;"
            "}"
            "QSlider::sub-page:horizontal {"
            "  background: #3284ff;"
            "  border-radius: 4px;"
            "}"
            "QSlider::handle:horizontal {"
            "  background: #f8fafc;"
            "  border: 1px solid #94a3b8;"
            "  width: 14px;"
            "  margin: -4px 0;"
            "  border-radius: 7px;"
            "}");

        // root — корневая вертикальная раскладка окна: верхняя панель,
        // 3D-график и нижняя информационная панель.
        QVBoxLayout* root = new QVBoxLayout(this);
        root->setContentsMargins(6, 6, 6, 6);
        root->setSpacing(6);

        // Q3DScatter — базовый 3D-график Qt DataVisualization, на котором мы
        // имитируем линии через плотные последовательности точек.
        graph_ = new Q3DScatter();

        // Выделение пользователем точек не нужно, потому что управление идет
        // через кнопки и ползунок.
        graph_->setSelectionMode(QAbstract3DGraph::SelectionNone);

        // Тени отключаем, чтобы сцена была чище и работала быстрее.
        graph_->setShadowQuality(QAbstract3DGraph::ShadowQualityNone);

        // Используем стандартную тему Qt как базу и дальше подправляем цвета.
        graph_->activeTheme()->setType(Q3DTheme::ThemeQt);
        graph_->activeTheme()->setLabelTextColor(QColor("#1f2937"));
        graph_->activeTheme()->setGridEnabled(true);
        graph_->activeTheme()->setBackgroundEnabled(true);
        graph_->setTitle(QString::fromUtf8("Метод Розенброка (3D)"));

        // Перспективная проекция обычно нагляднее для пространственной сцены,
        // чем ортографическая.
        graph_->setOrthoProjection(false);

        // Создаем по одной оси для каждого измерения видимой сцены.
        axisX_ = new QValue3DAxis();
        axisY_ = new QValue3DAxis();
        axisZ_ = new QValue3DAxis();

        // SegmentCount задает число крупных делений оси.
        axisX_->setSegmentCount(8);
        axisY_->setSegmentCount(8);
        axisZ_->setSegmentCount(8);

        // SubSegmentCount задает число малых делений внутри крупного сегмента.
        axisX_->setSubSegmentCount(3);
        axisY_->setSubSegmentCount(3);
        axisZ_->setSubSegmentCount(3);

        // Единый формат подписей осей с тремя знаками после запятой.
        axisX_->setLabelFormat("%.3f");
        axisY_->setLabelFormat("%.3f");
        axisZ_->setLabelFormat("%.3f");

        // Названия осей должны быть видимы, иначе в сменяемых проекциях
        // пользователь теряет контекст.
        axisX_->setTitleVisible(true);
        axisY_->setTitleVisible(true);
        axisZ_->setTitleVisible(true);

        // Привязываем созданные оси к самому графику.
        graph_->setAxisX(axisX_);
        graph_->setAxisY(axisY_);
        graph_->setAxisZ(axisZ_);

        // lineSeries_ рисует "сплошную" траекторию как плотную последовательность
        // маленьких сфер.
        lineSeries_ = new QScatter3DSeries();
        lineSeries_->setItemLabelFormat(QString());
        lineSeries_->setMesh(QAbstract3DSeries::MeshSphere);
        lineSeries_->setItemSize(0.011f);
        lineSeries_->setBaseColor(QColor(25, 25, 30));
        lineSeries_->setName(QString::fromUtf8("Путь (линия)"));

        // surfaceSeries_ хранит точечную аппроксимацию поверхности/среза.
        surfaceSeries_ = new QScatter3DSeries();
        surfaceSeries_->setItemLabelFormat(QString());
        surfaceSeries_->setMesh(QAbstract3DSeries::MeshSphere);
        surfaceSeries_->setMeshSmooth(true);
        surfaceSeries_->setItemSize(0.011f);
        surfaceSeries_->setBaseColor(QColor(90, 128, 210, 185));
        surfaceSeries_->setName(QString::fromUtf8("Поверхность (срез)"));
        surfaceSeries_->setVisible(true);

        // pathSeries_ выделяет сами узловые точки пути крупнее, чем lineSeries_.
        pathSeries_ = new QScatter3DSeries();
        pathSeries_->setItemLabelFormat(QString());
        pathSeries_->setMesh(QAbstract3DSeries::MeshSphere);
        pathSeries_->setItemSize(0.055f);
        pathSeries_->setBaseColor(QColor(15, 15, 20));
        pathSeries_->setName(QString::fromUtf8("Путь (точки)"));

        // s1Series_, s2Series_ и s3Series_ нужны для цветового разделения
        // удачных перемещений вдоль разных направлений базиса.
        s1Series_ = new QScatter3DSeries();
        s1Series_->setItemLabelFormat(QString());
        s1Series_->setMesh(QAbstract3DSeries::MeshSphere);
        s1Series_->setItemSize(0.012f);
        s1Series_->setBaseColor(QColor(36, 121, 255));
        s1Series_->setName(QString::fromUtf8("Шаги вдоль s1"));

        s2Series_ = new QScatter3DSeries();
        s2Series_->setItemLabelFormat(QString());
        s2Series_->setMesh(QAbstract3DSeries::MeshSphere);
        s2Series_->setItemSize(0.012f);
        s2Series_->setBaseColor(QColor(10, 170, 120));
        s2Series_->setName(QString::fromUtf8("Шаги вдоль s2"));

        s3Series_ = new QScatter3DSeries();
        s3Series_->setItemLabelFormat(QString());
        s3Series_->setMesh(QAbstract3DSeries::MeshSphere);
        s3Series_->setItemSize(0.012f);
        s3Series_->setBaseColor(QColor(165, 85, 255));
        s3Series_->setName(QString::fromUtf8("Шаги вдоль s3"));

        // activeSeries_ рисует текущий обрабатываемый сегмент шага.
        activeSeries_ = new QScatter3DSeries();
        activeSeries_->setItemLabelFormat(QString());
        activeSeries_->setMesh(QAbstract3DSeries::MeshSphere);
        activeSeries_->setItemSize(0.016f);
        activeSeries_->setBaseColor(QColor(245, 130, 32));
        activeSeries_->setName(QString::fromUtf8("Текущий шаг"));

        // failedSeries_ показывает последние неудачные пробные точки красным.
        failedSeries_ = new QScatter3DSeries();
        failedSeries_->setItemLabelFormat(QString());
        failedSeries_->setMesh(QAbstract3DSeries::MeshSphere);
        failedSeries_->setItemSize(0.048f);
        failedSeries_->setBaseColor(QColor(225, 40, 40));
        failedSeries_->setName(QString::fromUtf8("Неудачные шаги"));

        // currentSeries_ — один крупный маркер текущей базисной точки.
        currentSeries_ = new QScatter3DSeries();
        currentSeries_->setItemLabelFormat(QString());
        currentSeries_->setMesh(QAbstract3DSeries::MeshSphere);
        currentSeries_->setItemSize(0.090f);
        currentSeries_->setBaseColor(QColor(245, 130, 32));
        currentSeries_->setName(QString::fromUtf8("Текущая точка"));

        // Порядок addSeries(...) важен визуально: сначала фоновые серии, затем
        // поверх них более важные точки и активный шаг.
        graph_->addSeries(surfaceSeries_);
        graph_->addSeries(lineSeries_);
        graph_->addSeries(s1Series_);
        graph_->addSeries(s2Series_);
        graph_->addSeries(s3Series_);
        graph_->addSeries(activeSeries_);
        graph_->addSeries(pathSeries_);
        graph_->addSeries(failedSeries_);
        graph_->addSeries(currentSeries_);

        // createWindowContainer(...) встраивает QWindow-подобный 3D-график в
        // обычную иерархию QWidget.
        graphContainer_ = QWidget::createWindowContainer(graph_, this);
        graphContainer_->setMinimumSize(1000, 620);
        graphContainer_->setFocusPolicy(Qt::StrongFocus);

        // topPanel содержит проекцию, камеру, таймлайн и сервисные кнопки.
        QWidget* topPanel = new QWidget(this);
        topPanel->setObjectName("viewerTopPanel");
        QHBoxLayout* controlsTop = new QHBoxLayout(topPanel);
        controlsTop->setContentsMargins(6, 4, 6, 4);
        controlsTop->setSpacing(4);

        // projectionGroup объединяет подпись и выпадающий список проекций.
        QWidget* projectionGroup = new QWidget(topPanel);
        projectionGroup->setObjectName("viewerGroup");
        QHBoxLayout* projectionLayout = new QHBoxLayout(projectionGroup);
        projectionLayout->setContentsMargins(0, 0, 0, 0);
        projectionLayout->setSpacing(4);
        projectionLabel_ = new QLabel(QString::fromUtf8("Проекц."), topPanel);
        projectionLabel_->setObjectName("panelTitle");
        projectionCombo_ = new QComboBox(topPanel);
        // Первый режим показывает исходное пространство переменных.
        projectionCombo_->addItem(QString::fromUtf8("x1, x2, x3"));
        // Остальные три режима показывают срезы с осью f(x).
        projectionCombo_->addItem(QString::fromUtf8("x1, x2, f(x)"));
        projectionCombo_->addItem(QString::fromUtf8("x1, x3, f(x)"));
        projectionCombo_->addItem(QString::fromUtf8("x2, x3, f(x)"));
        projectionLabel_->setText(QString::fromUtf8("Проекция"));
        projectionCombo_->setMinimumWidth(170);
        projectionCombo_->setMaximumWidth(260);
        projectionCombo_->setToolTip(QString::fromUtf8("Какие оси отображать в 3D-пространстве."));
        projectionLayout->addWidget(projectionLabel_);
        projectionLayout->addWidget(projectionCombo_);

        // sep1 визуально отделяет блок проекций от блока камеры.
        QFrame* sep1 = new QFrame(topPanel);
        sep1->setObjectName("lineSep");
        sep1->setFrameShape(QFrame::VLine);

        // cameraGroup содержит быстрые кнопки вращения и масштаба камеры.
        QWidget* cameraGroup = new QWidget(topPanel);
        cameraGroup->setObjectName("viewerGroup");
        QHBoxLayout* cameraLayout = new QHBoxLayout(cameraGroup);
        cameraLayout->setContentsMargins(0, 0, 0, 0);
        cameraLayout->setSpacing(3);
        // cameraLabel — подпись группы, а не отдельный интерактивный элемент.
        QLabel* cameraLabel = new QLabel(QString::fromUtf8("Камера"), topPanel);
        cameraLabel->setObjectName("panelTitle");
        camLeftButton_ = new QPushButton(QString::fromUtf8("↺"), topPanel);
        camRightButton_ = new QPushButton(QString::fromUtf8("↻"), topPanel);
        camUpButton_ = new QPushButton(QString::fromUtf8("↑"), topPanel);
        camDownButton_ = new QPushButton(QString::fromUtf8("↓"), topPanel);
        camZoomOutButton_ = new QPushButton(QString::fromUtf8("−"), topPanel);
        camZoomInButton_ = new QPushButton(QString::fromUtf8("+"), topPanel);
        // controlRole управляет стилями кнопок через QSS-селекторы по property.
        camLeftButton_->setProperty("controlRole", "icon");
        camRightButton_->setProperty("controlRole", "icon");
        camUpButton_->setProperty("controlRole", "icon");
        camDownButton_->setProperty("controlRole", "icon");
        camZoomOutButton_->setProperty("controlRole", "icon");
        camZoomInButton_->setProperty("controlRole", "icon");
        camLeftButton_->setToolTip(QString::fromUtf8("Повернуть камеру влево"));
        camRightButton_->setToolTip(QString::fromUtf8("Повернуть камеру вправо"));
        camUpButton_->setToolTip(QString::fromUtf8("Поднять камеру"));
        camDownButton_->setToolTip(QString::fromUtf8("Опустить камеру"));
        camZoomOutButton_->setToolTip(QString::fromUtf8("Отдалить камеру"));
        camZoomInButton_->setToolTip(QString::fromUtf8("Приблизить камеру"));
        cameraLayout->addWidget(cameraLabel);
        cameraLayout->addWidget(camLeftButton_);
        cameraLayout->addWidget(camRightButton_);
        cameraLayout->addWidget(camUpButton_);
        cameraLayout->addWidget(camDownButton_);
        cameraLayout->addWidget(camZoomOutButton_);
        cameraLayout->addWidget(camZoomInButton_);

        // sep2 отделяет камеру от кнопок навигации по шагам.
        QFrame* sep2 = new QFrame(topPanel);
        sep2->setObjectName("lineSep");
        sep2->setFrameShape(QFrame::VLine);

        // timelineGroup управляет шагами анимации.
        QWidget* timelineGroup = new QWidget(topPanel);
        timelineGroup->setObjectName("viewerGroup");
        QHBoxLayout* timelineLayout = new QHBoxLayout(timelineGroup);
        timelineLayout->setContentsMargins(0, 0, 0, 0);
        timelineLayout->setSpacing(3);
        QLabel* timelineLabel = new QLabel(QString::fromUtf8("Шаги"), topPanel);
        timelineLabel->setObjectName("panelTitle");
        firstButton_ = new QPushButton(QString::fromUtf8("|<"), topPanel);
        prevButton_ = new QPushButton(QString::fromUtf8("<<"), topPanel);
        playPauseButton_ = new QPushButton(QString::fromUtf8("▶"), topPanel);
        nextButton_ = new QPushButton(QString::fromUtf8(">>"), topPanel);
        lastButton_ = new QPushButton(QString::fromUtf8(">|"), topPanel);
        // nav-роль у всех кнопок таймлайна оставляет их визуально однородными.
        firstButton_->setProperty("controlRole", "nav");
        prevButton_->setProperty("controlRole", "nav");
        nextButton_->setProperty("controlRole", "nav");
        lastButton_->setProperty("controlRole", "nav");
        playPauseButton_->setProperty("controlRole", "accent");
        firstButton_->setToolTip(QString::fromUtf8("К началу"));
        prevButton_->setToolTip(QString::fromUtf8("Предыдущий шаг"));
        playPauseButton_->setToolTip(QString::fromUtf8("Автовоспроизведение"));
        nextButton_->setToolTip(QString::fromUtf8("Следующий шаг"));
        lastButton_->setToolTip(QString::fromUtf8("К последнему шагу"));
        timelineLayout->addWidget(timelineLabel);
        timelineLayout->addWidget(firstButton_);
        timelineLayout->addWidget(prevButton_);
        timelineLayout->addWidget(playPauseButton_);
        timelineLayout->addWidget(nextButton_);
        timelineLayout->addWidget(lastButton_);

        // sep3 отделяет таймлайн от сервисных действий окна.
        QFrame* sep3 = new QFrame(topPanel);
        sep3->setObjectName("lineSep");
        sep3->setFrameShape(QFrame::VLine);

        // serviceGroup объединяет второстепенные сервисные кнопки.
        QWidget* serviceGroup = new QWidget(topPanel);
        serviceGroup->setObjectName("viewerGroup");
        QHBoxLayout* serviceLayout = new QHBoxLayout(serviceGroup);
        serviceLayout->setContentsMargins(0, 0, 0, 0);
        serviceLayout->setSpacing(3);
        resetViewButton_ = new QPushButton(QString::fromUtf8("Сброс камеры"), topPanel);
        closeButton_ = new QPushButton(QString::fromUtf8("Закрыть"), topPanel);
        closeButton_->setProperty("controlRole", "danger");
        resetViewButton_->setToolTip(QString::fromUtf8("Сбросить положение камеры"));
        closeButton_->setToolTip(QString::fromUtf8("Закрыть окно графика"));
        serviceLayout->addWidget(resetViewButton_);
        serviceLayout->addWidget(closeButton_);

        // iterationLabel_ в верхней панели показывает текущие индексы Ш и k.
        iterationLabel_ = new QLabel(QString::fromUtf8("Шаг"), topPanel);
        iterationLabel_->setObjectName("iterLabel");
        iterationLabel_->setMinimumWidth(200);

        // Добавляем группы в том порядке, в каком они должны читаться слева
        // направо пользователем.
        controlsTop->addWidget(projectionGroup);
        controlsTop->addWidget(sep1);
        controlsTop->addWidget(cameraGroup);
        controlsTop->addWidget(sep2);
        controlsTop->addWidget(timelineGroup);
        controlsTop->addWidget(sep3);
        controlsTop->addWidget(iterationLabel_);
        controlsTop->addStretch(1);
        controlsTop->addWidget(serviceGroup);
        root->addWidget(topPanel);
        root->addWidget(graphContainer_, 1);

        // bottomPanel содержит ползунки и длинную статусную строку.
        QWidget* bottomPanel = new QWidget(this);
        bottomPanel->setObjectName("viewerBottomPanel");
        QVBoxLayout* bottomPanelLayout = new QVBoxLayout(bottomPanel);
        bottomPanelLayout->setContentsMargins(7, 4, 7, 3);
        bottomPanelLayout->setSpacing(2);

        // controlsBottom — горизонтальная панель управления шагом и скоростью.
        QHBoxLayout* controlsBottom = new QHBoxLayout();
        controlsBottom->setSpacing(5);
        QLabel* sliderLabel = new QLabel(QString::fromUtf8("Шаг"), bottomPanel);
        sliderLabel->setObjectName("panelTitle");
        stepSlider_ = new QSlider(Qt::Horizontal, bottomPanel);
        // Ползунок шага проходит от начальной позиции 0 до конца трассы.
        stepSlider_->setRange(0, static_cast<int>(stepMax_));
        stepSlider_->setValue(static_cast<int>(stepShown_));
        stepSlider_->setMinimumWidth(350);
        stepSlider_->setTickPosition(QSlider::NoTicks);
        // TickInterval рассчитывается приблизительно так, чтобы не было слишком
        // частой сетки на длинной трассе.
        stepSlider_->setTickInterval(std::max(1, static_cast<int>(stepMax_ / 20 + 1)));

        QLabel* speedTextLabel = new QLabel(QString::fromUtf8("Скорость (мс)"), bottomPanel);
        speedTextLabel->setObjectName("panelTitle");
        speedSlider_ = new QSlider(Qt::Horizontal, bottomPanel);
        // Скорость задается в миллисекундах между кадрами автопрокрутки.
        speedSlider_->setRange(100, 1300);
        speedSlider_->setValue(380);
        speedSlider_->setFixedWidth(120);
        speedValueLabel_ = new QLabel(QString::number(speedSlider_->value()), bottomPanel);
        speedValueLabel_->setObjectName("speedValueLabel");

        controlsBottom->addWidget(sliderLabel);
        controlsBottom->addWidget(stepSlider_, 1);
        controlsBottom->addSpacing(12);
        controlsBottom->addWidget(speedTextLabel);
        controlsBottom->addWidget(speedSlider_);
        controlsBottom->addWidget(speedValueLabel_);
        bottomPanelLayout->addLayout(controlsBottom);

        // infoLabel_ — длинная однострочная сводка по текущему состоянию сцены.
        infoLabel_ = new QLabel(bottomPanel);
        infoLabel_->setObjectName("infoLabel");
        infoLabel_->setWordWrap(false);
        infoLabel_->setMaximumHeight(20);
        bottomPanelLayout->addWidget(infoLabel_);
        root->addWidget(bottomPanel);

        // buttonList нужен, чтобы одинаково проставить cursor shape всем
        // кликабельным кнопкам интерфейса.
        std::vector<QPushButton*> buttonList;
        buttonList.push_back(camLeftButton_);
        buttonList.push_back(camRightButton_);
        buttonList.push_back(camUpButton_);
        buttonList.push_back(camDownButton_);
        buttonList.push_back(camZoomOutButton_);
        buttonList.push_back(camZoomInButton_);
        buttonList.push_back(firstButton_);
        buttonList.push_back(prevButton_);
        buttonList.push_back(playPauseButton_);
        buttonList.push_back(nextButton_);
        buttonList.push_back(lastButton_);
        buttonList.push_back(resetViewButton_);
        buttonList.push_back(closeButton_);
        for (std::size_t i = 0; i < buttonList.size(); ++i)
        {
            // Перед установкой курсора проверяем, что указатель действительно
            // не нулевой.
            if (buttonList[i] != NULL)
            {
                buttonList[i]->setCursor(Qt::PointingHandCursor);
            }
        }

        // timer_ управляет автопрокруткой шагов при нажатии play.
        timer_ = new QTimer(this);
        timer_->setInterval(speedSlider_->value());

        // Далее связываем все кнопки и ползунки с соответствующими слотами.
        QObject::connect(firstButton_, &QPushButton::clicked, this, &Rosenbrock3DViewerWidget::OnFirstClicked);
        QObject::connect(prevButton_, &QPushButton::clicked, this, &Rosenbrock3DViewerWidget::OnPrevClicked);
        QObject::connect(nextButton_, &QPushButton::clicked, this, &Rosenbrock3DViewerWidget::OnNextClicked);
        QObject::connect(lastButton_, &QPushButton::clicked, this, &Rosenbrock3DViewerWidget::OnLastClicked);
        QObject::connect(playPauseButton_,
                         &QPushButton::clicked,
                         this,
                         &Rosenbrock3DViewerWidget::OnPlayPauseClicked);
        QObject::connect(resetViewButton_,
                         &QPushButton::clicked,
                         this,
                         &Rosenbrock3DViewerWidget::OnResetViewClicked);
        QObject::connect(camLeftButton_, &QPushButton::clicked, this, &Rosenbrock3DViewerWidget::OnCamLeftClicked);
        QObject::connect(camRightButton_, &QPushButton::clicked, this, &Rosenbrock3DViewerWidget::OnCamRightClicked);
        QObject::connect(camUpButton_, &QPushButton::clicked, this, &Rosenbrock3DViewerWidget::OnCamUpClicked);
        QObject::connect(camDownButton_, &QPushButton::clicked, this, &Rosenbrock3DViewerWidget::OnCamDownClicked);
        QObject::connect(camZoomOutButton_,
                         &QPushButton::clicked,
                         this,
                         &Rosenbrock3DViewerWidget::OnCamZoomOutClicked);
        QObject::connect(camZoomInButton_,
                         &QPushButton::clicked,
                         this,
                         &Rosenbrock3DViewerWidget::OnCamZoomInClicked);
        QObject::connect(closeButton_, &QPushButton::clicked, this, &Rosenbrock3DViewerWidget::OnCloseClicked);
        QObject::connect(stepSlider_, &QSlider::valueChanged, this, &Rosenbrock3DViewerWidget::OnStepSliderChanged);
        QObject::connect(speedSlider_,
                         &QSlider::valueChanged,
                         this,
                         &Rosenbrock3DViewerWidget::OnSpeedSliderChanged);
        QObject::connect(timer_, &QTimer::timeout, this, &Rosenbrock3DViewerWidget::OnTimerTick);
        QObject::connect(projectionCombo_,
                         static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
                         this,
                         &Rosenbrock3DViewerWidget::OnProjectionChanged);

        // Начинаем в режиме пространства переменных x1, x2, x3.
        ApplyProjectionFromComboIndex(0);

        // После выбора проекции сразу настраиваем оси под текущую трассу.
        ApplyProjectionBounds();

        // Камеру ставим в удобный исходный ракурс.
        ResetCamera();

        // Первая отрисовка формирует все серии данных и подписи.
        UpdatePlot();

        // Только после успешной полной сборки интерфейса помечаем виджет как
        // готовый к показу.
        ready_ = true;
    }

    // IsReady() позволяет вызывающему коду понять, удалось ли полностью
    // собрать виджет.
    bool IsReady() const
    {
        return ready_;
    }

    // ErrorText() возвращает внутреннее сообщение об ошибке при неуспехе.
    const std::string& ErrorText() const
    {
        return error_;
    }

protected:
    // resizeEvent(...) пока не переопределяет поведение, но оставлен как
    // точка расширения на случай будущей адаптации сцены под размер окна.
    void resizeEvent(QResizeEvent* event)
    {
        QWidget::resizeEvent(event);
    }

private:
    // ------------------------------------------------------------------------
    // МЕТОД EVALUATEFUNCTION
    // ------------------------------------------------------------------------
    // Назначение:
    // Безопасно вычислить значение функции в точке, отфильтровав нечисловые
    // результаты и нулевой указатель на функцию.
    // ------------------------------------------------------------------------
    bool EvaluateFunction(const Vector& point, double& outValue) const
    {
        // На всякий случай заранее обнуляем выходное значение.
        outValue = 0.0;

        // Без самой функции построить f(x)-проекции невозможно.
        if (function_ == NULL)
        {
            return false;
        }

        // evalError нужен только как буфер для API функции.
        std::string evalError;

        // Если вычисление функции в точке не удалось, вызывающий код просто
        // пропустит такую точку.
        if (!function_->Evaluate(point, outValue, evalError))
        {
            return false;
        }

        // Даже успешный вызов Evaluate(...) дополнительно фильтруем от NaN/inf.
        if (!std::isfinite(outValue))
        {
            return false;
        }

        // true означает, что outValue теперь содержит корректное конечное число.
        return true;
    }

    // ------------------------------------------------------------------------
    // МЕТОД APPLYPROJECTIONFROMCOMBOINDEX
    // ------------------------------------------------------------------------
    // Назначение:
    // Перевести индекс выбранной проекции в пару осей и флаг использования
    // проекции с осью функции.
    // ------------------------------------------------------------------------
    void ApplyProjectionFromComboIndex(int index)
    {
        // Индекс 0 означает чистое пространство переменных.
        if (index == 0)
        {
            useFunctionProjection_ = false;
            axisA_ = 0;
            axisB_ = 1;
        }
        else if (index == 2)
        {
            // Индекс 2 соответствует проекции x1, x3, f(x).
            useFunctionProjection_ = true;
            axisA_ = 0;
            axisB_ = 2;
        }
        else if (index == 3)
        {
            // Индекс 3 соответствует проекции x2, x3, f(x).
            useFunctionProjection_ = true;
            axisA_ = 1;
            axisB_ = 2;
        }
        else
        {
            // Ветка else покрывает индекс 1 и любые неожиданные значения как
            // проекцию x1, x2, f(x).
            useFunctionProjection_ = true;
            axisA_ = 0;
            axisB_ = 1;
        }
    }

    // ------------------------------------------------------------------------
    // МЕТОД TOPLOTPOINT
    // ------------------------------------------------------------------------
    // Назначение:
    // Преобразовать математическую точку и, при необходимости, значение
    // функции в фактическую 3D-точку сцены Qt.
    // ------------------------------------------------------------------------
    QVector3D ToPlotPoint(const Vector& point, double fValue) const
    {
        // В режиме обычного пространства переменных используем x1, x2, x3
        // как есть.
        if (!useFunctionProjection_)
        {
            // Если координат меньше трех, вернуть полноценную точку нельзя.
            if (point.size() < 3)
            {
                return QVector3D();
            }

            // QVector3D хранит float-компоненты, поэтому double явно сужаем.
            return QVector3D(static_cast<float>(point[0]),
                             static_cast<float>(point[1]),
                             static_cast<float>(point[2]));
        }

        // В режиме проекции на поверхность ось Y занята значением функции.
        return QVector3D(static_cast<float>(point[axisA_]), static_cast<float>(fValue), static_cast<float>(point[axisB_]));
    }

    // ------------------------------------------------------------------------
    // МЕТОД APPENDPOINT
    // ------------------------------------------------------------------------
    // Назначение:
    // Добавить одну точку в Qt-массив данных scatter-серии.
    // ------------------------------------------------------------------------
    void AppendPoint(QScatterDataArray& array, const QVector3D& p) const
    {
        // QScatter3DSeries хранит элементы не как QVector3D, а как
        // QScatterDataItem.
        QScatterDataItem item;

        // setPosition(...) записывает фактическую 3D-позицию точки.
        item.setPosition(p);

        // push_back(...) помещает готовый item в конец массива серии.
        array.push_back(item);
    }

    // ------------------------------------------------------------------------
    // МЕТОД APPENDINTERPOLATEDSEGMENT
    // ------------------------------------------------------------------------
    // Назначение:
    // Аппроксимировать отрезок `a -> b` плотной последовательностью точек,
    // чтобы в scatter-графике он визуально выглядел как линия.
    // ------------------------------------------------------------------------
    void AppendInterpolatedSegment(QScatterDataArray& array,
                                   const QVector3D& a,
                                   const QVector3D& b,
                                   int pieces) const
    {
        // Минимум две части нужен, чтобы отрезок не выродился в одну точку.
        if (pieces < 2)
        {
            pieces = 2;
        }

        // Индекс интерполяции объявляем отдельно в стиле проекта.
        int i = 0;
        for (i = 0; i <= pieces; ++i)
        {
            // t пробегает интервал [0, 1] равномерно.
            const float t = static_cast<float>(i) / static_cast<float>(pieces);

            // Линейная интерполяция между началом и концом отрезка.
            const QVector3D p = a * (1.0f - t) + b * t;
            AppendPoint(array, p);
        }
    }

    // ------------------------------------------------------------------------
    // МЕТОД APPLYPROJECTIONBOUNDS
    // ------------------------------------------------------------------------
    // Назначение:
    // Настроить диапазоны и названия осей в соответствии с текущей проекцией.
    // ------------------------------------------------------------------------
    void ApplyProjectionBounds()
    {
        // bounds будет содержать итоговый охватывающий параллелепипед сцены.
        Bounds3D bounds;

        // Для режима поверхности и для режима пространства переменных границы
        // считаются разными helper-функциями.
        if (useFunctionProjection_)
        {
            bounds = BuildBoundsForProjection(result_, axisA_, axisB_);
        }
        else
        {
            bounds = BuildBoundsForVariableSpace(result_);
        }

        // Передаем рассчитанные границы напрямую трем осям сцены.
        axisX_->setRange(static_cast<float>(bounds.xMin), static_cast<float>(bounds.xMax));
        axisY_->setRange(static_cast<float>(bounds.yMin), static_cast<float>(bounds.yMax));
        axisZ_->setRange(static_cast<float>(bounds.zMin), static_cast<float>(bounds.zMax));

        // Названия осей должны совпадать с реально выбранной геометрией сцены.
        if (useFunctionProjection_)
        {
            axisX_->setTitle(VariableName(axisA_));
            axisY_->setTitle(QString::fromUtf8("f(x)"));
            axisZ_->setTitle(VariableName(axisB_));
        }
        else
        {
            axisX_->setTitle(QString::fromUtf8("x1"));
            axisY_->setTitle(QString::fromUtf8("x2"));
            axisZ_->setTitle(QString::fromUtf8("x3"));
        }
    }

    // ------------------------------------------------------------------------
    // МЕТОД RESETCAMERA
    // ------------------------------------------------------------------------
    // Назначение:
    // Вернуть камеру в стабильный обзорный ракурс, охватывающий всю сцену.
    // ------------------------------------------------------------------------
    void ResetCamera()
    {
        // До обращения к камере проверяем всю цепочку указателей сцены.
        if (graph_ == NULL || graph_->scene() == NULL || graph_->scene()->activeCamera() == NULL)
        {
            return;
        }

        // camera — активная камера текущей 3D-сцены.
        Q3DCamera* camera = graph_->scene()->activeCamera();
        Bounds3D bounds;

        // Центр камеры зависит от текущего режима проекции.
        if (useFunctionProjection_)
        {
            bounds = BuildBoundsForProjection(result_, axisA_, axisB_);
        }
        else
        {
            bounds = BuildBoundsForVariableSpace(result_);
        }

        // center — геометрический центр ограничивающего бокса сцены.
        const QVector3D center(static_cast<float>(0.5 * (bounds.xMin + bounds.xMax)),
                               static_cast<float>(0.5 * (bounds.yMin + bounds.yMax)),
                               static_cast<float>(0.5 * (bounds.zMin + bounds.zMax)));

        // setTarget(...) направляет взгляд камеры в центр данных.
        camera->setTarget(center);

        // Готовый изометрический пресет дает достаточно наглядный стартовый вид.
        camera->setCameraPreset(Q3DCamera::CameraPresetIsometricRightHigh);

        // Стартовый zoom подбирается эмпирически как удобный обзорный масштаб.
        camera->setZoomLevel(105.0f);
    }

    // ------------------------------------------------------------------------
    // МЕТОД SETSTEPSHOWN
    // ------------------------------------------------------------------------
    // Назначение:
    // Установить текущий показываемый шаг и при необходимости синхронизировать
    // с ним ползунок UI.
    // ------------------------------------------------------------------------
    void SetStepShown(std::size_t value, bool syncSlider)
    {
        // Запрещаем выходить за диапазон реальной трассировки.
        if (value > stepMax_)
        {
            value = stepMax_;
        }

        // Сохраняем новый текущий шаг.
        stepShown_ = value;

        // Если нужно, обновляем и визуальный ползунок.
        if (syncSlider && stepSlider_ != NULL)
        {
            // Избегаем лишней установки значения, чтобы не запускать каскад
            // сигналов без необходимости.
            if (stepSlider_->value() != static_cast<int>(stepShown_))
            {
                // blockSignals(true) временно отключает сигнал valueChanged,
                // чтобы не получить рекурсивный вызов SetStepShown(...).
                const bool old = stepSlider_->blockSignals(true);
                stepSlider_->setValue(static_cast<int>(stepShown_));
                stepSlider_->blockSignals(old);
            }
        }

        // После смены шага сцена и все подписи должны перестроиться.
        UpdatePlot();
    }

    // Кнопка "в начало" сразу сбрасывает автопрокрутку и прыгает к шагу 0.
    void OnFirstClicked()
    {
        timer_->stop();
        SetStepShown(0, true);
    }

    // Кнопка "назад" останавливает таймер и сдвигает просмотр на шаг назад.
    void OnPrevClicked()
    {
        timer_->stop();
        if (stepShown_ > 0)
        {
            SetStepShown(stepShown_ - 1, true);
        }
    }

    // Кнопка "вперед" переходит к следующему шагу, если конец еще не достигнут.
    void OnNextClicked()
    {
        timer_->stop();
        if (stepShown_ < stepMax_)
        {
            SetStepShown(stepShown_ + 1, true);
        }
    }

    // Кнопка "в конец" показывает последнюю доступную строку трассы.
    void OnLastClicked()
    {
        timer_->stop();
        SetStepShown(stepMax_, true);
    }

    // Play/Pause либо запускает автоматический просмотр, либо его останавливает.
    void OnPlayPauseClicked()
    {
        // Если таймер уже работает, повторное нажатие переводит в паузу.
        if (timer_->isActive())
        {
            timer_->stop();
            UpdatePlot();
            return;
        }

        // Если пользователь уже стоял на последнем шаге, начинаем новую
        // анимацию с начала.
        if (stepShown_ >= stepMax_)
        {
            SetStepShown(0, true);
        }

        // timer_->start() запускает периодические вызовы OnTimerTick().
        timer_->start();
        UpdatePlot();
    }

    // Кнопка сброса вида возвращает оси и камеру к исходному положению.
    void OnResetViewClicked()
    {
        ApplyProjectionBounds();
        ResetCamera();
    }

    // ------------------------------------------------------------------------
    // МЕТОД ADJUSTCAMERA
    // ------------------------------------------------------------------------
    // Назначение:
    // Изменить углы камеры и/или zoom на заданные приращения.
    // ------------------------------------------------------------------------
    void AdjustCamera(float deltaXRotation, float deltaYRotation, float deltaZoom)
    {
        if (graph_ == NULL || graph_->scene() == NULL || graph_->scene()->activeCamera() == NULL)
        {
            return;
        }

        Q3DCamera* camera = graph_->scene()->activeCamera();
        if (deltaXRotation != 0.0f)
        {
            float xRot = camera->xRotation() + deltaXRotation;
            // Ограничиваем наклон, чтобы камера не переворачивалась.
            if (xRot < -85.0f)
            {
                xRot = -85.0f;
            }
            else if (xRot > 85.0f)
            {
                xRot = 85.0f;
            }
            camera->setXRotation(xRot);
        }
        if (deltaYRotation != 0.0f)
        {
            camera->setYRotation(camera->yRotation() + deltaYRotation);
        }
        if (deltaZoom != 0.0f)
        {
            float zoom = camera->zoomLevel() + deltaZoom;
            // Слишком маленький и слишком большой zoom ограничиваем.
            if (zoom < 1.0f)
            {
                zoom = 1.0f;
            }
            else if (zoom > 500.0f)
            {
                zoom = 500.0f;
            }
            camera->setZoomLevel(zoom);
        }
    }

    // ------------------------------------------------------------------------
    // МЕТОД ADJUSTZOOMBYFACTOR
    // ------------------------------------------------------------------------
    // Назначение:
    // Изменить масштаб камеры мультипликативно, а не аддитивно.
    // ------------------------------------------------------------------------
    void AdjustZoomByFactor(float factor)
    {
        if (graph_ == NULL || graph_->scene() == NULL || graph_->scene()->activeCamera() == NULL)
        {
            return;
        }
        if (!(factor > 0.0f))
        {
            return;
        }

        Q3DCamera* camera = graph_->scene()->activeCamera();
        float zoom = camera->zoomLevel() * factor;
        if (zoom < 1.0f)
        {
            zoom = 1.0f;
        }
        else if (zoom > 500.0f)
        {
            zoom = 500.0f;
        }
        camera->setZoomLevel(zoom);
    }

    // Кнопки камеры — тонкие обертки над общими helper-методами.
    void OnCamLeftClicked()
    {
        AdjustCamera(0.0f, -10.0f, 0.0f);
    }
    void OnCamRightClicked()
    {
        AdjustCamera(0.0f, 10.0f, 0.0f);
    }
    void OnCamUpClicked()
    {
        AdjustCamera(-8.0f, 0.0f, 0.0f);
    }
    void OnCamDownClicked()
    {
        AdjustCamera(8.0f, 0.0f, 0.0f);
    }
    void OnCamZoomOutClicked()
    {
        AdjustZoomByFactor(0.88f);
    }
    void OnCamZoomInClicked()
    {
        AdjustZoomByFactor(1.14f);
    }

    // Закрытие окна также останавливает таймер, чтобы не было фоновой анимации.
    void OnCloseClicked()
    {
        timer_->stop();
        close();
    }

    // Ползунок шага напрямую управляет текущим показанным шагом.
    void OnStepSliderChanged(int value)
    {
        timer_->stop();
        // Слайдер не должен уводить шаг в отрицательную область.
        if (value < 0)
        {
            value = 0;
        }
        SetStepShown(static_cast<std::size_t>(value), false);
    }

    // Ползунок скорости меняет интервал таймера автопрокрутки.
    void OnSpeedSliderChanged(int value)
    {
        // Слишком маленькие интервалы приводили бы к дерганой анимации.
        if (value < 50)
        {
            value = 50;
        }
        timer_->setInterval(value);
        speedValueLabel_->setText(QString::number(value));
    }

    // Каждый тик таймера либо двигает просмотр вперед, либо останавливает его.
    void OnTimerTick()
    {
        if (stepShown_ >= stepMax_)
        {
            timer_->stop();
            UpdatePlot();
            return;
        }
        SetStepShown(stepShown_ + 1, true);
    }

    // Смена проекции требует пересчитать оси, камеру и сами серии данных.
    void OnProjectionChanged(int index)
    {
        ApplyProjectionFromComboIndex(index);
        ApplyProjectionBounds();
        ResetCamera();
        UpdatePlot();
    }

    // ------------------------------------------------------------------------
    // МЕТОД UPDATEPLOT
    // ------------------------------------------------------------------------
    // Назначение:
    // Полностью пересобрать все серии 3D-графика для текущего шага анимации.
    // ------------------------------------------------------------------------
    void UpdatePlot()
    {
        // state хранит уже вычисленную визуальную сводку до текущего шага.
        StepVisualState3D state;
        BuildStateUntilStep3D(result_, stepShown_, state);

        // По умолчанию текущая точка берется из построенного состояния пути.
        Vector shownCurrentPoint = state.currentPoint;

        // hasShownCurrent определяет, нужно ли рисовать крупный текущий маркер.
        bool hasShownCurrent = state.hasCurrentPoint;

        // activeColor — цвет активного сегмента текущего шага.
        QColor activeColor(245, 130, 32);

        // Если уже есть конкретная строка трассы для текущего шага, уточняем
        // цвет и фактическую текущую точку по этой строке.
        if (state.hasRow && stepShown_ > 0 && stepShown_ <= result_.trace.size())
        {
            const OptimizationResult::TraceRow& row = result_.trace[stepShown_ - 1];
            if (!row.successfulStep)
            {
                activeColor = QColor(225, 40, 40);
            }
            else if (row.j == 1)
            {
                activeColor = QColor(36, 121, 255);
            }
            else if (row.j == 2)
            {
                activeColor = QColor(10, 170, 120);
            }
            else if (row.j == 3)
            {
                activeColor = QColor(165, 85, 255);
            }

            // fnDim нужен, чтобы сопоставлять точки с размерностью функции.
            const std::size_t fnDim = function_ != NULL ? function_->Dimension() : 0;

            // После удачного шага текущей логично считать именно новую trialPoint.
            if (row.successfulStep && row.trialPoint.size() == fnDim && fnDim >= 2)
            {
                shownCurrentPoint = row.trialPoint;
                hasShownCurrent = true;
            }
            else if (row.yi.size() == fnDim && fnDim >= 2)
            {
                // Иначе хотя бы остаемся на базисной точке y_i текущей строки.
                shownCurrentPoint = row.yi;
                hasShownCurrent = true;
            }
        }

        // Для каждой серии создаем новый массив данных, который целиком
        // заменит старый набор точек через resetArray(...).
        QScatterDataArray* surfaceArray = new QScatterDataArray();
        QScatterDataArray* pathArray = new QScatterDataArray();
        QScatterDataArray* lineArray = new QScatterDataArray();
        QScatterDataArray* s1Array = new QScatterDataArray();
        QScatterDataArray* s2Array = new QScatterDataArray();
        QScatterDataArray* s3Array = new QScatterDataArray();
        QScatterDataArray* activeArray = new QScatterDataArray();
        QScatterDataArray* failedArray = new QScatterDataArray();
        QScatterDataArray* currentArray = new QScatterDataArray();

        // Поверхность строим только для режимов, где одна из осей — f(x).
        if (useFunctionProjection_ && function_ != NULL)
        {
            const std::size_t n = function_->Dimension();

            // Проекция корректна только если обе оси лежат в диапазоне
            // размерности функции и не совпадают между собой.
            if ((n == 2 || n == 3) && axisA_ < n && axisB_ < n && axisA_ != axisB_)
            {
                Vector basePoint;

                // Если в трассе есть стартовая точка правильной размерности,
                // фиксируем срез именно по ней.
                if (!result_.trace.empty() && result_.trace[0].xk.size() == n)
                {
                    // Use the original problem setup (start point x0) for a stable slice.
                    basePoint = result_.trace[0].xk;
                }
                else if (result_.optimumX.size() == n)
                {
                    // Если трасса почему-то пустая, используем оптимум как
                    // запасную базовую точку для построения среза.
                    basePoint = result_.optimumX;
                }
                else
                {
                    // Последний fallback — нулевой вектор нужной размерности.
                    basePoint.assign(n, 0.0);
                }

                const Bounds3D bounds = BuildBoundsForProjection(result_, axisA_, axisB_);

                // Чем больше gridA/gridB, тем более монолитно выглядит
                // точечная аппроксимация поверхности.
                const int gridA = 220;
                const int gridB = 220;

                // da и db — шаги сетки вдоль двух независимых осей среза.
                const double da = (bounds.xMax - bounds.xMin) / static_cast<double>(gridA - 1);
                const double db = (bounds.zMax - bounds.zMin) / static_cast<double>(gridB - 1);

                // reserve(...) уменьшает число перераспределений памяти при
                // добавлении десятков тысяч точек поверхности.
                surfaceArray->reserve(static_cast<qsizetype>(gridA * gridB));

                int ia = 0;
                for (ia = 0; ia < gridA; ++ia)
                {
                    const double varA = bounds.xMin + da * static_cast<double>(ia);
                    int ib = 0;
                    for (ib = 0; ib < gridB; ++ib)
                    {
                        const double varB = bounds.zMin + db * static_cast<double>(ib);

                        // sample начинается с фиксированной базовой точки среза.
                        Vector sample = basePoint;

                        // Затем в sample подставляются две переменные текущего
                        // узла сетки среза.
                        sample[axisA_] = varA;
                        sample[axisB_] = varB;

                        double fValue = 0.0;
                        if (EvaluateFunction(sample, fValue))
                        {
                            AppendPoint(*surfaceArray, ToPlotPoint(sample, fValue));
                        }
                    }
                }
            }
        }

        // accepted3D — временный вектор уже преобразованных 3D-точек пути.
        std::vector<QVector3D> accepted3D;
        accepted3D.reserve(state.acceptedPath.size());
        pathArray->reserve(static_cast<qsizetype>(state.acceptedPath.size()));

        std::size_t i = 0;
        for (i = 0; i < state.acceptedPath.size(); ++i)
        {
            QVector3D p;
            if (useFunctionProjection_)
            {
                double fValue = 0.0;
                if (!EvaluateFunction(state.acceptedPath[i], fValue))
                {
                    continue;
                }
                p = ToPlotPoint(state.acceptedPath[i], fValue);
            }
            else
            {
                if (state.acceptedPath[i].size() < 3)
                {
                    continue;
                }
                p = ToPlotPoint(state.acceptedPath[i], 0.0);
            }

            // accepted3D используется для построения плотной линии между
            // соседними принятыми узловыми точками.
            accepted3D.push_back(p);

            // pathArray хранит только сами опорные точки пути.
            AppendPoint(*pathArray, p);
        }

        // Между соседними узлами пути дорисовываем плотный псевдоотрезок.
        for (i = 1; i < accepted3D.size(); ++i)
        {
            const QVector3D a = accepted3D[i - 1];
            const QVector3D b = accepted3D[i];
            const float dist = (b - a).length();

            // Чем длиннее сегмент, тем больше intermediate points нужно для
            // визуально гладкой линии.
            int pieces = static_cast<int>(std::lround(static_cast<double>(dist) * 70.0));
            pieces = std::clamp(pieces, 26, 160);
            AppendInterpolatedSegment(*lineArray, a, b, pieces);
        }

        // rowsToProcess ограничивает просмотренную часть трассы текущим шагом.
        const std::size_t rowsToProcess = std::min(stepShown_, result_.trace.size());

        // recentFailed хранит хвост последних неудачных точек до ближайшего
        // успешного шага.
        std::vector<QVector3D> recentFailed;
        recentFailed.reserve(rowsToProcess);
        for (i = 0; i < rowsToProcess; ++i)
        {
            const OptimizationResult::TraceRow& row = result_.trace[i];
            if (row.yi.size() < 3 || row.trialPoint.size() < 3)
            {
                continue;
            }

            QVector3D from;
            QVector3D to;
            if (useFunctionProjection_)
            {
                double fFrom = 0.0;
                double fTo = 0.0;
                if (!EvaluateFunction(row.yi, fFrom) || !EvaluateFunction(row.trialPoint, fTo))
                {
                    continue;
                }
                from = ToPlotPoint(row.yi, fFrom);
                to = ToPlotPoint(row.trialPoint, fTo);
            }
            else
            {
                from = ToPlotPoint(row.yi, 0.0);
                to = ToPlotPoint(row.trialPoint, 0.0);
            }

            // Удачный шаг рисуем цветом соответствующего направления.
            if (row.successfulStep)
            {
                // После удачного шага история промахов обнуляется.
                recentFailed.clear();
                const float dist = (to - from).length();
                int pieces = static_cast<int>(std::lround(static_cast<double>(dist) * 90.0));
                pieces = std::clamp(pieces, 28, 180);

                if (row.j == 1)
                {
                    AppendInterpolatedSegment(*s1Array, from, to, pieces);
                }
                else if (row.j == 2)
                {
                    AppendInterpolatedSegment(*s2Array, from, to, pieces);
                }
                else if (row.j == 3)
                {
                    AppendInterpolatedSegment(*s3Array, from, to, pieces);
                }
            }
            else
            {
                // Неудачные попытки оставляем только как красные точки.
                recentFailed.push_back(to);
            }
        }

        // Все накопленные промахи переносим в отдельную красную серию.
        failedArray->reserve(static_cast<qsizetype>(recentFailed.size()));
        for (i = 0; i < recentFailed.size(); ++i)
        {
            AppendPoint(*failedArray, recentFailed[i]);
        }

        // activeArray рисует именно текущий обрабатываемый сегмент шага.
        if (state.hasRow && stepShown_ > 0 && stepShown_ <= result_.trace.size())
        {
            const OptimizationResult::TraceRow& row = result_.trace[stepShown_ - 1];
            if (row.yi.size() >= 3 && row.trialPoint.size() >= 3)
            {
                QVector3D from;
                QVector3D to;

                // canDraw отсекает случаи, когда для сегмента не удалось
                // корректно вычислить координаты.
                bool canDraw = true;
                if (useFunctionProjection_)
                {
                    double fFrom = 0.0;
                    double fTo = 0.0;
                    canDraw = EvaluateFunction(row.yi, fFrom) && EvaluateFunction(row.trialPoint, fTo);
                    if (canDraw)
                    {
                        from = ToPlotPoint(row.yi, fFrom);
                        to = ToPlotPoint(row.trialPoint, fTo);
                    }
                }
                else
                {
                    from = ToPlotPoint(row.yi, 0.0);
                    to = ToPlotPoint(row.trialPoint, 0.0);
                }
                if (canDraw)
                {
                    const float dist = (to - from).length();
                    int pieces = static_cast<int>(std::lround(static_cast<double>(dist) * 110.0));
                    pieces = std::clamp(pieces, 36, 220);
                    AppendInterpolatedSegment(*activeArray, from, to, pieces);
                }
            }
        }

        // currentArray всегда содержит не более одной крупной текущей точки.
        if (hasShownCurrent)
        {
            const std::size_t fnDim = function_ != NULL ? function_->Dimension() : 0;
            if (!useFunctionProjection_)
            {
                currentArray->reserve(1);
                AppendPoint(*currentArray, ToPlotPoint(shownCurrentPoint, 0.0));
            }
            else
            {
                double fValue = 0.0;
                if (shownCurrentPoint.size() == fnDim && EvaluateFunction(shownCurrentPoint, fValue))
                {
                    currentArray->reserve(1);
                    AppendPoint(*currentArray, ToPlotPoint(shownCurrentPoint, fValue));
                }
            }
        }

        // resetArray(...) полностью заменяет содержимое каждой серии новым
        // набором точек и одновременно берет на себя владение массивом.
        surfaceSeries_->dataProxy()->resetArray(surfaceArray);
        lineSeries_->dataProxy()->resetArray(lineArray);
        pathSeries_->dataProxy()->resetArray(pathArray);
        s1Series_->dataProxy()->resetArray(s1Array);
        s2Series_->dataProxy()->resetArray(s2Array);
        s3Series_->dataProxy()->resetArray(s3Array);
        activeSeries_->setBaseColor(activeColor);
        activeSeries_->dataProxy()->resetArray(activeArray);
        failedSeries_->dataProxy()->resetArray(failedArray);
        currentSeries_->dataProxy()->resetArray(currentArray);

        // title — заголовок 3D-графика, зависящий от активной проекции.
        QString title;
        if (useFunctionProjection_)
        {
            title = QString::fromUtf8("Метод Розенброка (3D): ") + VariableName(axisA_) +
                    QString::fromUtf8(", ") + VariableName(axisB_) + QString::fromUtf8(", f(x)");
        }
        else
        {
            title = QString::fromUtf8("Метод Розенброка (3D): x1, x2, x3");
        }
        if (state.hasRow)
        {
            title += QString::fromUtf8("  |  k=") + QString::number(static_cast<unsigned long long>(state.k)) +
                     QString::fromUtf8(", j=") + QString::number(static_cast<unsigned long long>(state.j));
        }
        graph_->setTitle(title);

        // iterText дублирует в верхней панели текущий шаг и текущую итерацию.
        const QString iterText = QString::fromUtf8("Ш ") +
                                 QString::number(static_cast<unsigned long long>(stepShown_)) +
                                 QString::fromUtf8(" / ") +
                                 QString::number(static_cast<unsigned long long>(stepMax_)) +
                                 QString::fromUtf8("   k ") +
                                 QString::number(static_cast<unsigned long long>(state.k)) +
                                 QString::fromUtf8(" / ") +
                                 QString::number(static_cast<unsigned long long>(kMax_));
        iterationLabel_->setText(iterText);

        // info собирает нижнюю текстовую сводку по кадру.
        std::ostringstream info;
        info << "Удачных шагов: " << state.successfulCount << "   |   Неудачных: " << state.failedCount;
        if (useFunctionProjection_)
        {
            info << "   |   точек поверхности: " << surfaceArray->size();
        }
        if (hasShownCurrent)
        {
            double fValue = 0.0;
            if (EvaluateFunction(shownCurrentPoint, fValue))
            {
                info << "   |   x=" << FormatVector3(shownCurrentPoint);
                info << "   |   f(x)=" << std::fixed << std::setprecision(6) << fValue;
            }
        }
        if (state.hasRow)
        {
            info << "   |   Δ=" << std::fixed << std::setprecision(6) << state.delta;
            info << "   |   шаг: " << (state.successfulStep ? "успешный" : "неудачный");
            if (state.rollback)
            {
                info << "   |   откат: да";
            }
            if (state.directionChanged)
            {
                info << "   |   смена направления: да";
            }
        }
        info << "   |   3D: ЛКМ вращение, колесо масштаб, кнопки ↺↻↑↓";
        infoLabel_->setText(QString::fromStdString(info.str()));

        // Доступность кнопок зависит от положения на шкале шагов.
        firstButton_->setEnabled(stepShown_ > 0);
        prevButton_->setEnabled(stepShown_ > 0);
        nextButton_->setEnabled(stepShown_ < stepMax_);
        lastButton_->setEnabled(stepShown_ < stepMax_);

        // Play/Pause меняет подпись в зависимости от факта работы таймера.
        playPauseButton_->setText(timer_->isActive() ? QString::fromUtf8("❚❚")
                                                     : QString::fromUtf8("▶"));

        // Если внешний код передал callback, синхронизируем с ним текущий шаг.
        if (onStepChanged_)
        {
            onStepChanged_(stepShown_, stepMax_, state.k);
        }
    }

private:
    // Исходная функция для вычисления f(x) в отображаемых точках.
    const IObjectiveFunction* function_;

    // Ссылка на уже посчитанный результат оптимизации и его трассировку.
    const OptimizationResult& result_;

    // Максимальный номер итерации k в трассировке.
    std::size_t kMax_;

    // Общее число шагов, доступных для прокрутки.
    std::size_t stepMax_;

    // Текущий показываемый шаг.
    std::size_t stepShown_;

    // axisA_ и axisB_ задают, какие координаты используются в активной
    // проекции по горизонтальным осям.
    std::size_t axisA_;
    std::size_t axisB_;

    // useFunctionProjection_ показывает, участвует ли f(x) как ось Y.
    bool useFunctionProjection_;

    // ready_ сигнализирует, что конструктор завершил полную инициализацию.
    bool ready_;

    // error_ хранит внутреннее диагностическое сообщение при неудаче запуска.
    std::string error_;

    // graph_ — основной Qt DataVisualization-график.
    Q3DScatter* graph_;

    // graphContainer_ встраивает 3D-график в иерархию QWidget.
    QWidget* graphContainer_;

    // Три оси сцены.
    QValue3DAxis* axisX_;
    QValue3DAxis* axisY_;
    QValue3DAxis* axisZ_;

    // Серии данных для поверхности, линии пути, точек пути и вспомогательных
    // цветных маркеров.
    QScatter3DSeries* surfaceSeries_;
    QScatter3DSeries* lineSeries_;
    QScatter3DSeries* pathSeries_;
    QScatter3DSeries* s1Series_;
    QScatter3DSeries* s2Series_;
    QScatter3DSeries* s3Series_;
    QScatter3DSeries* activeSeries_;
    QScatter3DSeries* failedSeries_;
    QScatter3DSeries* currentSeries_;

    // Элементы верхней панели управления проекцией и камерой.
    QLabel* projectionLabel_;
    QComboBox* projectionCombo_;
    QPushButton* camLeftButton_;
    QPushButton* camRightButton_;
    QPushButton* camUpButton_;
    QPushButton* camDownButton_;
    QPushButton* camZoomOutButton_;
    QPushButton* camZoomInButton_;

    // Кнопки таймлайна шагов.
    QPushButton* firstButton_;
    QPushButton* prevButton_;
    QPushButton* playPauseButton_;
    QPushButton* nextButton_;
    QPushButton* lastButton_;
    QPushButton* resetViewButton_;
    QPushButton* closeButton_;

    // Ползунки шага и скорости анимации.
    QSlider* stepSlider_;
    QSlider* speedSlider_;
    QLabel* speedValueLabel_;

    // Текстовые метки верхней и нижней панели.
    QLabel* iterationLabel_;
    QLabel* infoLabel_;
    QLabel* basisOverlay_;

    // timer_ обеспечивает автопрокрутку при режиме play.
    QTimer* timer_;

    // Callback во внешнее окно для синхронизации таблицы с графиком.
    ViewerStepChangedCallback onStepChanged_;
};

// ----------------------------------------------------------------------------
// ФУНКЦИЯ SHOWROSENBROCK3DVIEWER
// ----------------------------------------------------------------------------
// Назначение:
// Создать и показать отдельное окно 3D-визуализации для задачи размерности 3.
// ----------------------------------------------------------------------------
bool ShowRosenbrock3DViewer(const IObjectiveFunction* function,
                            const OptimizationResult& result,
                            std::string& error,
                            std::size_t initialStep,
                            const ViewerStepChangedCallback& onStepChanged)
{
    // Сначала очищаем старый текст ошибки, если он был.
    error.clear();

    // Без функции невозможно построить ни траекторию с f(x), ни поверхность.
    if (function == NULL)
    {
        error = "Функция не задана";
        return false;
    }

    // Данный viewer рассчитан именно на функции трех переменных.
    if (function->Dimension() != 3)
    {
        error = "3D-визуализация поддерживает только задачи размерности 3";
        return false;
    }

    // Пустая трассировка означает, что показывать на графике нечего.
    if (result.trace.empty())
    {
        error = "Трассировка пуста: нечего визуализировать";
        return false;
    }

    // Перед запуском вьювера подготавливаем окружение Qt plugins.
    if (!PrepareQtRuntimePlugins(error))
    {
        return false;
    }

    // argc/argv нужны только если текущий процесс еще не имеет QApplication.
    int argc = 0;
    char** argv = NULL;

    // Если приложение уже работает в GUI-режиме, переиспользуем текущий
    // QApplication вместо создания нового.
    QApplication* app = qobject_cast<QApplication*>(QCoreApplication::instance());
    bool ownsApp = false;
    if (app == NULL)
    {
        app = new QApplication(argc, argv);
        ownsApp = true;
    }

    // Если QT_PLUGIN_PATH задан, явно добавляем его в search paths приложения.
    const QString pluginsPath = QString::fromLocal8Bit(qgetenv("QT_PLUGIN_PATH"));
    if (!pluginsPath.isEmpty())
    {
        app->addLibraryPath(pluginsPath);
    }

    // kMax нужен для верхней панели состояния внутри виджета.
    const std::size_t kMax = MaxIterationK(result);

    // view — само окно 3D-визуализации.
    Rosenbrock3DViewerWidget* view =
        new Rosenbrock3DViewerWidget(function, result, kMax, initialStep, onStepChanged, NULL);
    if (!view->IsReady())
    {
        error = view->ErrorText();
        delete view;
        if (ownsApp)
        {
            delete app;
        }
        return false;
    }

    // NonModal не блокирует главное окно лабораторной работы.
    view->setWindowModality(Qt::NonModal);

    // WA_DeleteOnClose поручает Qt удалить виджет автоматически при закрытии.
    view->setAttribute(Qt::WA_DeleteOnClose, true);
    view->show();
    view->activateWindow();
    view->raise();

    // Если QApplication был создан здесь же, нужно вручную запустить его
    // event loop и затем удалить объект приложения.
    if (ownsApp)
    {
        app->exec();
        delete app;
        return true;
    }

    // In GUI mode (QApplication already exists) do not block the main window.
    return true;
}

}  // namespace lab2

#else

#include <string>

namespace lab2
{

bool ShowRosenbrock3DViewer(const IObjectiveFunction*,
                            const OptimizationResult&,
                            std::string& error,
                            std::size_t,
                            const ViewerStepChangedCallback&)
{
    // В fallback-ветке просто объясняем пользователю, что модуль 3D недоступен.
    error = "Qt DataVisualization не подключен в текущей сборке";
    return false;
}

}  // namespace lab2

#endif
