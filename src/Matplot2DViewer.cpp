// ============================================================================
// ФАЙЛ MATPLOT2DVIEWER.CPP
// ============================================================================
// Назначение:
// Реализовать отдельное 2D-окно визуализации метода Розенброка на базе
// Qt Charts.
//
// Что содержит файл:
// 1) вспомогательные структуры и функции для подготовки 2D-данных;
// 2) интерактивный виджет графика с панорамированием и изолиниями;
// 3) виджет RosenbrockViewerWidget с панелью управления и анимацией;
// 4) публичную точку входа ShowRosenbrock2DViewer(...);
// 5) fallback-ветку на случай сборки без Qt Charts.
// ============================================================================

// Публичное объявление интерфейса 2D-вьювера.
#include "lab2/Matplot2DViewer.h"

#if LAB2_HAS_QTCHARTS

// Классы Qt Charts для 2D-графика, осей и серий.
#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QtCharts/QLegend>
#include <QtCharts/QLegendMarker>
#include <QtCharts/QLineSeries>
#include <QtCharts/QScatterSeries>
#include <QtCharts/QValueAxis>
// Базовые QtCore-типы для окружения, путей, таймеров и процессов.
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
// Графические классы для рисования, мыши и жестов.
#include <QtGui/QColor>
#include <QtGui/QMouseEvent>
#include <QtGui/QNativeGestureEvent>
#include <QtGui/QPainter>
#include <QtGui/QResizeEvent>
#include <QtGui/QWheelEvent>
// Виджетные классы для окна визуализации и панели управления.
#include <QtWidgets/QApplication>
#include <QtWidgets/QFrame>
#include <QtWidgets/QGestureEvent>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPinchGesture>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSlider>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

// Стандартная библиотека для численных helper-функций и контейнеров.
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
// СТРУКТУРА PLOTBOUNDS2D
// ----------------------------------------------------------------------------
// Назначение:
// Хранить численные границы видимой области по двум координатным осям.
// ----------------------------------------------------------------------------
struct PlotBounds2D
{
    // Минимум по оси X.
    double xMin;

    // Максимум по оси X.
    double xMax;

    // Минимум по оси Y.
    double yMin;

    // Максимум по оси Y.
    double yMax;
};

// ----------------------------------------------------------------------------
// СТРУКТУРА POINT2D
// ----------------------------------------------------------------------------
// Назначение:
// Представлять одну 2D-точку в координатах графика.
// ----------------------------------------------------------------------------
struct Point2D
{
    // Координата по оси X.
    double x;

    // Координата по оси Y.
    double y;
};

// ----------------------------------------------------------------------------
// СТРУКТУРА LINESEGMENT
// ----------------------------------------------------------------------------
// Назначение:
// Представлять один отрезок между двумя 2D-точками.
// ----------------------------------------------------------------------------
struct LineSegment
{
    // Начало отрезка.
    Point2D from;

    // Конец отрезка.
    Point2D to;
};

// ----------------------------------------------------------------------------
// СТРУКТУРА CONTOURGRID
// ----------------------------------------------------------------------------
// Назначение:
// Хранить регулярную сетку значений функции для дальнейшего построения
// изолиний.
// ----------------------------------------------------------------------------
struct ContourGrid
{
    // Значения координаты X на узлах сетки.
    std::vector<double> xAxis;

    // Значения координаты Y на узлах сетки.
    std::vector<double> yAxis;

    // Плоский массив значений функции в узлах сетки.
    std::vector<double> values;

    // Число узлов по оси X.
    std::size_t nx;

    // Число узлов по оси Y.
    std::size_t ny;

    // Минимальное конечное значение функции на сетке.
    double minZ;

    // Максимальное конечное значение функции на сетке.
    double maxZ;
};

// ----------------------------------------------------------------------------
// СТРУКТУРА CONTOURSEGMENT
// ----------------------------------------------------------------------------
// Назначение:
// Описывать один уже вычисленный сегмент изолинии вместе с его цветом.
// ----------------------------------------------------------------------------
struct ContourSegment
{
    // Начало сегмента изолинии.
    Point2D from;

    // Конец сегмента изолинии.
    Point2D to;

    // Цвет сегмента, зависящий от уровня.
    QColor color;
};

// Минимальная плотность сетки для изолиний.
const std::size_t CONTOUR_GRID_MIN = 76;

// Максимальная плотность сетки для изолиний.
const std::size_t CONTOUR_GRID_MAX = 156;

// Минимальное число уровней изолиний.
const std::size_t CONTOUR_LEVEL_MIN = 72;

// Максимальное число уровней изолиний.
const std::size_t CONTOUR_LEVEL_MAX = 180;

// ----------------------------------------------------------------------------
// ФУНКЦИЯ NICETICKSTEP
// ----------------------------------------------------------------------------
// Назначение:
// Подобрать "красивый" шаг делений оси по заданному диапазону и желаемому
// числу меток.
// ----------------------------------------------------------------------------
static double NiceTickStep(double span, int targetTicks)
{
    // Если диапазон некорректен или число тиков бессмысленно мало, отдаем
    // безопасный шаг 1.0.
    if (!std::isfinite(span) || span <= 0.0 || targetTicks < 2)
    {
        return 1.0;
    }

    // rawStep — точный шаг без округления к "красивым" значениям.
    const double rawStep = span / static_cast<double>(targetTicks - 1);

    // magnitude — порядок величины rawStep: 0.1, 1, 10 и т.д.
    const double magnitude = std::pow(10.0, std::floor(std::log10(rawStep)));

    // normalized переводит шаг в диапазон около [1; 10) для удобного выбора.
    const double normalized = rawStep / magnitude;

    // nice — базовый множитель из стандартного ряда 1, 2, 5, 10.
    double nice = 1.0;
    if (normalized >= 7.5)
    {
        nice = 10.0;
    }
    else if (normalized >= 3.5)
    {
        nice = 5.0;
    }
    else if (normalized >= 1.5)
    {
        nice = 2.0;
    }

    // Возвращаем итоговый "красивый" шаг в исходном масштабе.
    return nice * magnitude;
}

// ----------------------------------------------------------------------------
// ФУНКЦИЯ DECIMALSFORSTEP
// ----------------------------------------------------------------------------
// Назначение:
// Подобрать количество знаков после запятой для подписей оси по шагу сетки.
// ----------------------------------------------------------------------------
static int DecimalsForStep(double step)
{
    // Некорректному шагу сопоставляем достаточно безопасную точность.
    if (!std::isfinite(step) || step <= 0.0)
    {
        return 6;
    }

    // Для шага от единицы и выше дробная часть на подписи обычно не нужна.
    if (step >= 1.0)
    {
        return 0;
    }

    // Для маленьких шагов оцениваем, сколько десятичных знаков надо показать.
    const int decimals = static_cast<int>(std::ceil(-std::log10(step))) + 1;

    // Ограничиваем диапазон, чтобы подписи не становились нечитаемо длинными.
    return std::clamp(decimals, 0, 12);
}

// ----------------------------------------------------------------------------
// ФУНКЦИЯ STABLETICKSTEP
// ----------------------------------------------------------------------------
// Назначение:
// Обновить шаг тиков с небольшой гистерезисной стабилизацией, чтобы подписи
// осей не "дрожали" от каждого малейшего масштаба.
// ----------------------------------------------------------------------------
static double StableTickStep(double previousStep, double span, int targetTicks)
{
    // desired — новый идеальный шаг, вычисленный без учета истории.
    const double desired = NiceTickStep(span, targetTicks);

    // Если прошлого шага нет, используем стартовый шаг чуть мельче.
    if (!(previousStep > 0.0) || !std::isfinite(previousStep))
    {
        // На первом запуске делаем сетку немного плотнее (меньшая цена деления),
        // но без чрезмерного наложения подписей.
        return desired * 0.5;
    }

    // ratio показывает, насколько новый шаг отличается от прошлого.
    const double ratio = desired / previousStep;
    if (!std::isfinite(ratio) || ratio <= 0.0)
    {
        return desired;
    }

    // hysteresis задает "мертвую зону", где шаг не пересчитывается.
    const double hysteresis = 1.20;
    if (ratio > hysteresis || ratio < (1.0 / hysteresis))
    {
        return desired;
    }
    return previousStep;
}

// ----------------------------------------------------------------------------
// ФУНКЦИЯ UPDATEBOUNDSBYPOINT
// ----------------------------------------------------------------------------
// Назначение:
// Расширить текущие границы так, чтобы они включали очередную 2D-точку.
// ----------------------------------------------------------------------------
static void UpdateBoundsByPoint(PlotBounds2D& bounds, const Vector& p)
{
    // Для 2D-вьювера нужны как минимум две координаты.
    if (p.size() < 2)
    {
        return;
    }

    // При необходимости сдвигаем левую границу.
    if (p[0] < bounds.xMin)
    {
        bounds.xMin = p[0];
    }

    // При необходимости сдвигаем правую границу.
    if (p[0] > bounds.xMax)
    {
        bounds.xMax = p[0];
    }

    // При необходимости сдвигаем нижнюю границу.
    if (p[1] < bounds.yMin)
    {
        bounds.yMin = p[1];
    }

    // При необходимости сдвигаем верхнюю границу.
    if (p[1] > bounds.yMax)
    {
        bounds.yMax = p[1];
    }
}

// ----------------------------------------------------------------------------
// ФУНКЦИЯ BUILDBOUNDS
// ----------------------------------------------------------------------------
// Назначение:
// Построить стартовую область обзора по всей траектории метода.
// ----------------------------------------------------------------------------
static PlotBounds2D BuildBounds(const OptimizationResult& result)
{
    PlotBounds2D bounds;

    // Стартуем с бесконечностей, чтобы затем корректно сужать диапазон.
    bounds.xMin = std::numeric_limits<double>::infinity();
    bounds.xMax = -std::numeric_limits<double>::infinity();
    bounds.yMin = std::numeric_limits<double>::infinity();
    bounds.yMax = -std::numeric_limits<double>::infinity();

    std::size_t i = 0;
    for (i = 0; i < result.trace.size(); ++i)
    {
        // В границы включаем базовую точку итерации.
        UpdateBoundsByPoint(bounds, result.trace[i].xk);

        // Включаем текущую внутреннюю точку y_i.
        UpdateBoundsByPoint(bounds, result.trace[i].yi);

        // Пробную точку имеет смысл учитывать только если шаг оказался удачным
        // и вошел в реальную траекторию.
        if (result.trace[i].successfulStep)
        {
            UpdateBoundsByPoint(bounds, result.trace[i].trialPoint);
        }
    }

    // Дополнительно включаем найденный итоговый оптимум.
    UpdateBoundsByPoint(bounds, result.optimumX);

    // Если трасса оказалась пустой или некорректной, подставляем разумную
    // область по умолчанию.
    if (!std::isfinite(bounds.xMin) || !std::isfinite(bounds.xMax) || !std::isfinite(bounds.yMin) ||
        !std::isfinite(bounds.yMax))
    {
        bounds.xMin = -1.0;
        bounds.xMax = 1.0;
        bounds.yMin = -1.0;
        bounds.yMax = 1.0;
    }

    // dx и dy — реальные размеры охватываемой области без внешних отступов.
    const double dx = bounds.xMax - bounds.xMin;
    const double dy = bounds.yMax - bounds.yMin;

    // Добавляем запас, чтобы путь не прилипал к краям окна.
    const double padX = std::max(0.5, dx * 0.25);
    const double padY = std::max(0.5, dy * 0.25);

    bounds.xMin -= padX;
    bounds.xMax += padX;
    bounds.yMin -= padY;
    bounds.yMax += padY;

    return bounds;
}

// ----------------------------------------------------------------------------
// ФУНКЦИЯ MAXITERATIONK
// ----------------------------------------------------------------------------
// Назначение:
// Найти максимальный номер внешней итерации k в трассировке.
// ----------------------------------------------------------------------------
static std::size_t MaxIterationK(const OptimizationResult& result)
{
    // Если трасса неожиданно пуста, минимумом считаем первую итерацию.
    std::size_t maxK = 1;
    std::size_t i = 0;
    for (i = 0; i < result.trace.size(); ++i)
    {
        // Обновляем максимум по мере прохода по строкам трассы.
        if (result.trace[i].k > maxK)
        {
            maxK = result.trace[i].k;
        }
    }
    return maxK;
}

// ----------------------------------------------------------------------------
// ФУНКЦИЯ BUILDAXIS
// ----------------------------------------------------------------------------
// Назначение:
// Построить равномерную одномерную сетку между двумя значениями.
// ----------------------------------------------------------------------------
static std::vector<double> BuildAxis(double minValue, double maxValue, std::size_t count)
{
    std::vector<double> out;

    // Сразу резервируем память, чтобы избежать лишних realocation.
    out.reserve(count);

    // Для вырожденной сетки возвращаем единственную точку.
    if (count < 2)
    {
        out.push_back(minValue);
        return out;
    }

    // Равномерный шаг между соседними узлами.
    const double step = (maxValue - minValue) / static_cast<double>(count - 1);
    std::size_t i = 0;
    for (i = 0; i < count; ++i)
    {
        // Добавляем очередной узел сетки.
        out.push_back(minValue + step * static_cast<double>(i));
    }

    return out;
}

// ----------------------------------------------------------------------------
// ФУНКЦИЯ GRIDVALUE
// ----------------------------------------------------------------------------
// Назначение:
// Считать значение функции из плоского массива сетки по двум индексам.
// ----------------------------------------------------------------------------
static double GridValue(const ContourGrid& grid, std::size_t ix, std::size_t iy)
{
    // values хранится как одномерный массив, поэтому индекс разворачивается
    // вручную по формуле iy * nx + ix.
    return grid.values[iy * grid.nx + ix];
}

// ----------------------------------------------------------------------------
// ФУНКЦИЯ BUILDCONTOURGRID
// ----------------------------------------------------------------------------
// Назначение:
// Вычислить регулярную сетку значений функции на прямоугольной области для
// последующего построения изолиний.
// ----------------------------------------------------------------------------
static bool BuildContourGrid(const IObjectiveFunction* function,
                             const PlotBounds2D& bounds,
                             std::size_t gridSize,
                             ContourGrid& grid,
                             std::string& error)
{
    // По обеим осям используем одинаковую плотность сетки.
    grid.nx = gridSize;
    grid.ny = gridSize;

    // Строим координатные оси узлов сетки.
    grid.xAxis = BuildAxis(bounds.xMin, bounds.xMax, grid.nx);
    grid.yAxis = BuildAxis(bounds.yMin, bounds.yMax, grid.ny);

    // Инициализируем массив значений NaN-ами, чтобы легко отличать
    // невычисленные или некорректные узлы.
    grid.values.assign(grid.nx * grid.ny, std::numeric_limits<double>::quiet_NaN());

    // minZ и maxZ будут накапливать реальные пределы функции на сетке.
    double minZ = std::numeric_limits<double>::infinity();
    double maxZ = -std::numeric_limits<double>::infinity();

    // finiteCount считает число реально пригодных узлов.
    std::size_t finiteCount = 0;

    // point — временный 2D-вектор, который переиспользуется во всех узлах.
    Vector point(2, 0.0);
    std::size_t iy = 0;
    for (iy = 0; iy < grid.ny; ++iy)
    {
        std::size_t ix = 0;
        for (ix = 0; ix < grid.nx; ++ix)
        {
            // Записываем координаты текущего узла сетки.
            point[0] = grid.xAxis[ix];
            point[1] = grid.yAxis[iy];

            // value — значение функции в текущем узле.
            double value = 0.0;

            // evalError собирает сообщение функции, если вычисление не удалось.
            std::string evalError;

            // Узел принимается только при успешном вычислении и конечном
            // численном результате.
            if (function->Evaluate(point, value, evalError) && std::isfinite(value))
            {
                grid.values[iy * grid.nx + ix] = value;
                ++finiteCount;

                // Обновляем текущий минимум по сетке.
                if (value < minZ)
                {
                    minZ = value;
                }

                // Обновляем текущий максимум по сетке.
                if (value > maxZ)
                {
                    maxZ = value;
                }
            }
        }
    }

    // Для построения изолиний нужно хотя бы несколько валидных ячеек сетки.
    if (finiteCount < 4)
    {
        error = "Недостаточно точек для построения изолиний";
        return false;
    }

    // Сохраняем найденный диапазон значений в саму структуру сетки.
    grid.minZ = minZ;
    grid.maxZ = maxZ;
    return true;
}

// ----------------------------------------------------------------------------
// ФУНКЦИЯ INTERPOLATEPOINT
// ----------------------------------------------------------------------------
// Назначение:
// Найти точку пересечения уровня level с ребром между вершинами a и b.
// ----------------------------------------------------------------------------
static Point2D InterpolatePoint(const Point2D& a, double va, const Point2D& b, double vb, double level)
{
    Point2D out;

    // t — относительное положение точки пересечения на отрезке [a, b].
    double t = 0.5;

    // denom — разность значений функции на концах ребра.
    const double denom = vb - va;

    // Если перепад по значению не вырожден, используем обычную линейную
    // интерполяцию.
    if (std::fabs(denom) > 1e-12)
    {
        t = (level - va) / denom;
    }

    // Дополнительно зажимаем t в [0, 1], чтобы не уйти за край ребра из-за
    // численных погрешностей.
    if (t < 0.0)
    {
        t = 0.0;
    }
    if (t > 1.0)
    {
        t = 1.0;
    }

    // Восстанавливаем координаты точки пересечения по параметрическому виду
    // отрезка.
    out.x = a.x + t * (b.x - a.x);
    out.y = a.y + t * (b.y - a.y);
    return out;
}

// ----------------------------------------------------------------------------
// ФУНКЦИЯ INTERPOLATEEDGEPOINT
// ----------------------------------------------------------------------------
// Назначение:
// Выбрать нужное ребро ячейки и вычислить на нем точку пересечения изолинии.
// ----------------------------------------------------------------------------
static Point2D InterpolateEdgePoint(int edge,
                                    const Point2D& p0,
                                    const Point2D& p1,
                                    const Point2D& p2,
                                    const Point2D& p3,
                                    double z0,
                                    double z1,
                                    double z2,
                                    double z3,
                                    double level)
{
    // edge == 0 означает нижнее ребро ячейки p0 -> p1.
    if (edge == 0)
    {
        return InterpolatePoint(p0, z0, p1, z1, level);
    }

    // edge == 1 означает правое ребро p1 -> p2.
    if (edge == 1)
    {
        return InterpolatePoint(p1, z1, p2, z2, level);
    }

    // edge == 2 означает верхнее ребро p2 -> p3.
    if (edge == 2)
    {
        return InterpolatePoint(p2, z2, p3, z3, level);
    }

    // Во всех остальных случаях возвращаем левое ребро p3 -> p0.
    return InterpolatePoint(p3, z3, p0, z0, level);
}

// ----------------------------------------------------------------------------
// ФУНКЦИЯ CONTOURCOLOR
// ----------------------------------------------------------------------------
// Назначение:
// Преобразовать нормированное значение уровня t в цвет сегмента изолинии.
// ----------------------------------------------------------------------------
static QColor ContourColor(double t)
{
    // Сначала зажимаем параметр в допустимый диапазон.
    if (t < 0.0)
    {
        t = 0.0;
    }
    if (t > 1.0)
    {
        t = 1.0;
    }

    // Нижний цвет градиента.
    const int r0 = 230;
    const int g0 = 120;
    const int b0 = 90;

    // Верхний цвет градиента.
    const int r1 = 60;
    const int g1 = 120;
    const int b1 = 230;

    // Интерполируем каналы отдельно.
    const int r = static_cast<int>(r0 + (r1 - r0) * t);
    const int g = static_cast<int>(g0 + (g1 - g0) * t);
    const int b = static_cast<int>(b0 + (b1 - b0) * t);

    // Небольшая прозрачность делает линии мягче на светлой подложке.
    return QColor(r, g, b, 210);
}

// ----------------------------------------------------------------------------
// ФУНКЦИЯ ADDCONTOURSEGMENT
// ----------------------------------------------------------------------------
// Назначение:
// Добавить один валидный сегмент изолинии в итоговый массив сегментов.
// ----------------------------------------------------------------------------
static void AddContourSegment(std::vector<ContourSegment>& segments,
                              const Point2D& a,
                              const Point2D& b,
                              const QColor& color)
{
    // Неконечные координаты нельзя рисовать, поэтому сегмент отбрасываем.
    if (!std::isfinite(a.x) || !std::isfinite(a.y) || !std::isfinite(b.x) || !std::isfinite(b.y))
    {
        return;
    }

    // Вырожденный сегмент нулевой длины не несет полезной геометрии.
    if (std::fabs(a.x - b.x) < 1e-12 && std::fabs(a.y - b.y) < 1e-12)
    {
        return;
    }

    // seg — временная структура для одного валидного сегмента.
    ContourSegment seg;
    seg.from = a;
    seg.to = b;
    seg.color = color;

    // Добавляем сегмент в общий массив изолиний.
    segments.push_back(seg);
}

// ----------------------------------------------------------------------------
// ФУНКЦИЯ BUILDCONTOURLEVELS
// ----------------------------------------------------------------------------
// Назначение:
// Построить равномерный набор уровней изолиний между minZ и maxZ.
// ----------------------------------------------------------------------------
static std::vector<double> BuildContourLevels(double minZ, double maxZ, std::size_t levelCount)
{
    std::vector<double> levels;

    // При нулевом числе уровней возвращаем пустой список.
    if (levelCount == 0)
    {
        return levels;
    }

    // Если диапазон вырожден, искусственно расширяем его на 1.0.
    if (!(maxZ > minZ))
    {
        maxZ = minZ + 1.0;
    }

    // Шаг между соседними уровнями.
    const double step = (maxZ - minZ) / static_cast<double>(levelCount + 1);
    std::size_t i = 0;
    for (i = 1; i <= levelCount; ++i)
    {
        // Нулевой и крайний уровень не берем, чтобы не слипаться с краями.
        levels.push_back(minZ + step * static_cast<double>(i));
    }
    return levels;
}

// ----------------------------------------------------------------------------
// ФУНКЦИЯ BUILDCONTOURSEGMENTSFROMGRID
// ----------------------------------------------------------------------------
// Назначение:
// Построить набор сегментов изолиний по готовой сетке значений с помощью
// схемы marching squares.
// ----------------------------------------------------------------------------
static void BuildContourSegmentsFromGrid(const ContourGrid& grid,
                                         const std::vector<double>& levels,
                                         std::vector<ContourSegment>& segments)
{
    // Сначала удаляем старые сегменты.
    segments.clear();

    // Без корректного диапазона уровни строить бессмысленно.
    if (!(grid.maxZ > grid.minZ))
    {
        return;
    }

    // Если уровней нет, то и сегментов изолиний не будет.
    if (levels.empty())
    {
        return;
    }

    // minLevel и maxLevel используются для нормировки цвета.
    const double minLevel = levels.front();
    const double maxLevel = levels.back();

    // denom — знаменатель этой нормировки.
    const double denom = maxLevel - minLevel;

    std::size_t levelIndex = 0;
    for (levelIndex = 0; levelIndex < levels.size(); ++levelIndex)
    {
        const double level = levels[levelIndex];

        // Уровни вне реального диапазона сетки можно сразу пропустить.
        if (level < grid.minZ || level > grid.maxZ)
        {
            continue;
        }

        double t = 0.5;
        if (std::fabs(denom) > 1e-12)
        {
            t = (level - minLevel) / denom;
        }

        // color — цвет всех сегментов конкретного уровня.
        const QColor color = ContourColor(t);

        std::size_t iy = 0;
        for (iy = 0; iy + 1 < grid.ny; ++iy)
        {
            std::size_t ix = 0;
            for (ix = 0; ix + 1 < grid.nx; ++ix)
            {
                // p0..p3 — вершины текущей ячейки сетки.
                const Point2D p0{grid.xAxis[ix], grid.yAxis[iy]};
                const Point2D p1{grid.xAxis[ix + 1], grid.yAxis[iy]};
                const Point2D p2{grid.xAxis[ix + 1], grid.yAxis[iy + 1]};
                const Point2D p3{grid.xAxis[ix], grid.yAxis[iy + 1]};

                // z0..z3 — значения функции в вершинах той же ячейки.
                const double z0 = GridValue(grid, ix, iy);
                const double z1 = GridValue(grid, ix + 1, iy);
                const double z2 = GridValue(grid, ix + 1, iy + 1);
                const double z3 = GridValue(grid, ix, iy + 1);

                // Если хоть одно значение невалидно, ячейку пропускаем целиком.
                if (!std::isfinite(z0) || !std::isfinite(z1) || !std::isfinite(z2) || !std::isfinite(z3))
                {
                    continue;
                }

                // cell — 4-битный код конфигурации marching squares.
                int cell = 0;
                if (z0 > level)
                {
                    cell |= 1;
                }
                if (z1 > level)
                {
                    cell |= 2;
                }
                if (z2 > level)
                {
                    cell |= 4;
                }
                if (z3 > level)
                {
                    cell |= 8;
                }

                // Коды 0 и 15 означают, что изолиния ячейку не пересекает.
                if (cell == 0 || cell == 15)
                {
                    continue;
                }

                // e0..e3 — точки пересечения уровня с ребрами ячейки.
                const Point2D e0 = InterpolateEdgePoint(0, p0, p1, p2, p3, z0, z1, z2, z3, level);
                const Point2D e1 = InterpolateEdgePoint(1, p0, p1, p2, p3, z0, z1, z2, z3, level);
                const Point2D e2 = InterpolateEdgePoint(2, p0, p1, p2, p3, z0, z1, z2, z3, level);
                const Point2D e3 = InterpolateEdgePoint(3, p0, p1, p2, p3, z0, z1, z2, z3, level);

                // center помогает разрешать двусмысленные конфигурации 5 и 10.
                const double center = 0.25 * (z0 + z1 + z2 + z3);

                // По коду ячейки добавляем один или два сегмента изолинии.
                switch (cell)
                {
                    case 1:
                    case 14:
                        AddContourSegment(segments, e3, e0, color);
                        break;
                    case 2:
                    case 13:
                        AddContourSegment(segments, e0, e1, color);
                        break;
                    case 3:
                    case 12:
                        AddContourSegment(segments, e3, e1, color);
                        break;
                    case 4:
                    case 11:
                        AddContourSegment(segments, e1, e2, color);
                        break;
                    case 6:
                    case 9:
                        AddContourSegment(segments, e0, e2, color);
                        break;
                    case 7:
                    case 8:
                        AddContourSegment(segments, e3, e2, color);
                        break;
                    case 5:
                        if (center < level) {
                            AddContourSegment(segments, e3, e0, color);
                            AddContourSegment(segments, e1, e2, color);
                        } else {
                            AddContourSegment(segments, e0, e1, color);
                            AddContourSegment(segments, e2, e3, color);
                        }
                        break;
                    case 10:
                        if (center < level) {
                            AddContourSegment(segments, e0, e1, color);
                            AddContourSegment(segments, e2, e3, color);
                        } else {
                            AddContourSegment(segments, e3, e0, color);
                            AddContourSegment(segments, e1, e2, color);
                        }
                        break;
                    default:
                        break;
                }
            }
        }
    }
}

// ----------------------------------------------------------------------------
// ФУНКЦИЯ HASCOCOAPLUGIN
// ----------------------------------------------------------------------------
// Назначение:
// Проверить, есть ли в указанной папке macOS platform plugin `libqcocoa`.
// ----------------------------------------------------------------------------
static bool HasCocoaPlugin(const QString& platformsPath)
{
    // Пустой путь заведомо не может содержать плагин.
    if (platformsPath.isEmpty())
    {
        return false;
    }

    // pluginFile — полный путь до ожидаемого бинарника platform plugin.
    const QString pluginFile = QDir(platformsPath).filePath("libqcocoa.dylib");

    // QFileInfo удобен для проверки существования и типа файла.
    const QFileInfo info(pluginFile);
    return info.exists() && info.isFile();
}

// ----------------------------------------------------------------------------
// ФУНКЦИЯ SPLITPATHLIST
// ----------------------------------------------------------------------------
// Назначение:
// Разбить переменную окружения со списком путей на очищенный список директорий.
// ----------------------------------------------------------------------------
static QStringList SplitPathList(const QByteArray& rawValue)
{
    QStringList paths;

    // fromLocal8Bit корректно переводит байты окружения в QString на текущей
    // локали системы.
    const QString value = QString::fromLocal8Bit(rawValue);
    if (value.isEmpty())
    {
        return paths;
    }

    // listSeparator() выбирает корректный разделитель для текущей ОС.
    const QStringList parts = value.split(QDir::listSeparator(), Qt::SkipEmptyParts);
    int i = 0;
    for (i = 0; i < parts.size(); ++i)
    {
        const QString path = QDir::cleanPath(parts[i].trimmed());
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
// Найти директорию `platforms`, из которой Qt сможет загрузить plugin cocoa.
// ----------------------------------------------------------------------------
static QString FindQtPlatformsPath()
{
    // candidates — список всех потенциальных директорий-кандидатов.
    QStringList candidates;

#ifdef LAB2_QT_PLATFORMS_HINT
    const QString cmakeHint = QString::fromUtf8(LAB2_QT_PLATFORMS_HINT);
    if (!cmakeHint.isEmpty())
    {
        candidates.push_back(QDir::cleanPath(cmakeHint));
    }
#endif

    // envPath — путь, явно переданный через переменную окружения Qt.
    const QString envPath = QString::fromLocal8Bit(qgetenv("QT_QPA_PLATFORM_PLUGIN_PATH"));
    if (!envPath.isEmpty())
    {
        candidates.push_back(QDir::cleanPath(envPath));
    }

    // envPluginRoots — корневые plugin-пути из QT_PLUGIN_PATH.
    const QStringList envPluginRoots = SplitPathList(qgetenv("QT_PLUGIN_PATH"));
    int i = 0;
    for (i = 0; i < envPluginRoots.size(); ++i)
    {
        candidates.push_back(QDir(envPluginRoots[i]).filePath("platforms"));
    }

    // QLibraryInfo возвращает встроенный plugin-путь из текущей сборки Qt.
    const QString qlibPlugins = QLibraryInfo::path(QLibraryInfo::PluginsPath);
    if (!qlibPlugins.isEmpty())
    {
        candidates.push_back(QDir(qlibPlugins).filePath("platforms"));
    }

    // Отдельно пробуем спросить Homebrew, если Qt установлен через brew.
    QProcess brewProcess;
    brewProcess.start(QString::fromUtf8("brew"), QStringList() << QString::fromUtf8("--prefix")
                                                               << QString::fromUtf8("qtbase"));
    if (brewProcess.waitForFinished(1500) && brewProcess.exitCode() == 0)
    {
        const QString brewQtBase =
            QString::fromLocal8Bit(brewProcess.readAllStandardOutput()).trimmed();
        if (!brewQtBase.isEmpty())
        {
            candidates.push_back(QDir::cleanPath(QDir(brewQtBase).filePath("share/qt/plugins/platforms")));
        }
    }

    // appPluginsPath покрывает сценарий запуска из папки сборки рядом с plugins.
    const QString appPluginsPath = QDir(QCoreApplication::applicationDirPath()).filePath("../plugins/platforms");
    if (!appPluginsPath.isEmpty())
    {
        candidates.push_back(QDir::cleanPath(appPluginsPath));
    }

    // uniqueCandidates нужен, чтобы один и тот же путь не проверять по кругу.
    QStringList uniqueCandidates;
    for (i = 0; i < candidates.size(); ++i)
    {
        const QString path = QDir::cleanPath(candidates[i]);
        if (!path.isEmpty() && !uniqueCandidates.contains(path))
        {
            uniqueCandidates.push_back(path);
        }
    }

    // Возвращаем первый путь, в котором реально лежит cocoa plugin.
    for (i = 0; i < uniqueCandidates.size(); ++i)
    {
        if (HasCocoaPlugin(uniqueCandidates[i]))
        {
            return uniqueCandidates[i];
        }
    }

    return QString();
}

// ----------------------------------------------------------------------------
// ФУНКЦИЯ PREPAREQTRUNTIMEPLUGINS
// ----------------------------------------------------------------------------
// Назначение:
// Подготовить переменные окружения Qt так, чтобы при запуске нашлись platform
// plugins, прежде всего `cocoa` на macOS.
// ----------------------------------------------------------------------------
static bool PrepareQtRuntimePlugins(std::string& error)
{
#if defined(__APPLE__)
    const QString platformsPath = FindQtPlatformsPath();
    if (platformsPath.isEmpty())
    {
        error = "Не найден Qt platform plugin libqcocoa.dylib. Проверьте установку qtbase.";
        return false;
    }

    // pluginsDir сначала указывает на platforms, затем поднимается на уровень
    // выше в корень plugin-директории Qt.
    QDir pluginsDir(platformsPath);
    pluginsDir.cdUp();

    // pluginsPath — корневая директория plugins, а platformsPath — путь до
    // подпапки platforms.
    const QString pluginsPath = pluginsDir.absolutePath();

    // qputenv задает переменные окружения текущему процессу до инициализации Qt.
    qputenv("QT_PLUGIN_PATH", pluginsPath.toUtf8());
    qputenv("QT_QPA_PLATFORM_PLUGIN_PATH", platformsPath.toUtf8());
    qputenv("QT_QPA_PLATFORM", "cocoa");
#endif
    return true;
}

// ----------------------------------------------------------------------------
// СТРУКТУРА STEPVISUALSTATE
// ----------------------------------------------------------------------------
// Назначение:
// Хранить полностью подготовленное визуальное состояние текущего шага 2D-
// анимации: путь, неудачные попытки, базис и служебную статистику.
// ----------------------------------------------------------------------------
struct StepVisualState
{
    // acceptedPath содержит уже принятые точки траектории.
    std::vector<Vector> acceptedPath;

    // failedS1Segments хранит неудачные попытки по первому направлению.
    std::vector<LineSegment> failedS1Segments;

    // failedS2Segments хранит неудачные попытки по второму направлению.
    std::vector<LineSegment> failedS2Segments;

    // currentPoint — точка, выделяемая как текущая на рисунке.
    Point2D currentPoint;

    // hasCurrentPoint показывает, заполнена ли currentPoint корректно.
    bool hasCurrentPoint;

    // hasRow показывает, соответствует ли shownStep реальной строке трассы.
    bool hasRow;

    // k — номер внешней итерации метода.
    std::size_t k;

    // j — номер шага внутри текущей итерации.
    std::size_t j;

    // delta — текущее значение длины шага вдоль направления.
    double delta;

    // fyi — значение функции в текущей точке y_i.
    double fyi;

    // fTrial — значение функции в пробной точке.
    double fTrial;

    // successfulStep показывает, оказался ли текущий шаг удачным.
    bool successfulStep;

    // rollback отражает наличие отката на текущем проходе.
    bool rollback;

    // directionChanged отражает факт перестройки направлений.
    bool directionChanged;

    // hasNewBasis говорит, есть ли базис для показа в inset-окне.
    bool hasNewBasis;

    // newBasisOrigin — опорная точка для показа нового базиса.
    Point2D newBasisOrigin;

    // newBasisDirections — сами направления базиса.
    std::vector<Vector> newBasisDirections;

    // successfulCount — число удачных шагов на показанном префиксе.
    std::size_t successfulCount;

    // failedCount — число неудачных шагов на показанном префиксе.
    std::size_t failedCount;

    // Конструктор по умолчанию переводит структуру в пустое безопасное состояние.
    StepVisualState()
        : hasCurrentPoint(false),
          hasRow(false),
          k(0),
          j(0),
          delta(0.0),
          fyi(0.0),
          fTrial(0.0),
          successfulStep(false),
          rollback(false),
          directionChanged(false),
          hasNewBasis(false),
          successfulCount(0),
          failedCount(0)
    {
    }
};

// ----------------------------------------------------------------------------
// ФУНКЦИЯ BUILDSTATEUNTILSTEP
// ----------------------------------------------------------------------------
// Назначение:
// Построить визуальное состояние 2D-траектории от начала до заданного шага
// показа включительно.
// ----------------------------------------------------------------------------
static void BuildStateUntilStep(const OptimizationResult& result,
                                std::size_t shownStep,
                                StepVisualState& out)
{
    // Сначала полностью сбрасываем предыдущее визуальное состояние.
    out = StepVisualState();

    // Без трассы строить нечего.
    if (result.trace.empty())
    {
        return;
    }

    // current — текущая базовая точка, от которой строится принятый путь.
    Vector current = result.trace[0].yi;
    if (current.size() < 2)
    {
        return;
    }

    // Первая точка пути всегда совпадает с начальным y.
    out.acceptedPath.push_back(current);
    out.currentPoint.x = current[0];
    out.currentPoint.y = current[1];
    out.hasCurrentPoint = true;

    // Всегда показываем текущий базис, даже если перестройки не было.
    if (!result.trace[0].basisDirections.empty() && result.trace[0].basisOrigin.size() >= 2)
    {
        out.hasNewBasis = true;
        out.newBasisOrigin.x = result.trace[0].basisOrigin[0];
        out.newBasisOrigin.y = result.trace[0].basisOrigin[1];
        out.newBasisDirections = result.trace[0].basisDirections;
    }
    else if (!result.trace[0].newBasisDirections.empty() && result.trace[0].newBasisOrigin.size() >= 2)
    {
        // Совместимость со старыми трассами.
        out.hasNewBasis = true;
        out.newBasisOrigin.x = result.trace[0].newBasisOrigin[0];
        out.newBasisOrigin.y = result.trace[0].newBasisOrigin[1];
        out.newBasisDirections = result.trace[0].newBasisDirections;
    }

    // shownStep не должен выходить за фактическую длину трассы.
    if (shownStep > result.trace.size())
    {
        shownStep = result.trace.size();
    }

    std::size_t i = 0;
    for (i = 0; i < shownStep; ++i)
    {
        const OptimizationResult::TraceRow& row = result.trace[i];

        // Удачный шаг продвигает основную траекторию.
        if (row.successfulStep)
        {
            ++out.successfulCount;
            // Неудачные (пунктирные) отрезки считаем временными:
            // держим их до первого удачного шага, после чего очищаем.
            out.failedS1Segments.clear();
            out.failedS2Segments.clear();
            if (row.trialPoint.size() >= 2)
            {
                current = row.trialPoint;
                out.acceptedPath.push_back(current);
                out.currentPoint.x = current[0];
                out.currentPoint.y = current[1];
                out.hasCurrentPoint = true;
            }
        }
        else
        {
            ++out.failedCount;
            if (row.yi.size() >= 2 && row.trialPoint.size() >= 2)
            {
                LineSegment seg;
                seg.from.x = row.yi[0];
                seg.from.y = row.yi[1];
                seg.to.x = row.trialPoint[0];
                seg.to.y = row.trialPoint[1];
                if (row.j == 1)
                {
                    out.failedS1Segments.push_back(seg);
                }
                else if (row.j == 2)
                {
                    out.failedS2Segments.push_back(seg);
                }
            }
        }

        // При наличии нового базиса обновляем inset-виджет базиса.
        if (!row.basisDirections.empty() && row.basisOrigin.size() >= 2)
        {
            out.hasNewBasis = true;
            out.newBasisOrigin.x = row.basisOrigin[0];
            out.newBasisOrigin.y = row.basisOrigin[1];
            out.newBasisDirections = row.basisDirections;
        }
        else if (row.directionChanged && !row.newBasisDirections.empty() && row.newBasisOrigin.size() >= 2)
        {
            // Совместимость со старыми трассами.
            out.hasNewBasis = true;
            out.newBasisOrigin.x = row.newBasisOrigin[0];
            out.newBasisOrigin.y = row.newBasisOrigin[1];
            out.newBasisDirections = row.newBasisDirections;
        }
    }

    // Если уже показана хотя бы одна строка трассы, записываем ее как текущую.
    if (shownStep > 0)
    {
        const OptimizationResult::TraceRow& last = result.trace[shownStep - 1];
        out.hasRow = true;
        out.k = last.k;
        out.j = last.j;
        out.delta = last.delta;
        out.fyi = last.fyi;
        out.fTrial = last.fTrial;
        out.successfulStep = last.successfulStep;
        out.rollback = last.rollback;
        out.directionChanged = last.directionChanged;
        if (!last.basisDirections.empty() && last.basisOrigin.size() >= 2)
        {
            out.hasNewBasis = true;
            out.newBasisOrigin.x = last.basisOrigin[0];
            out.newBasisOrigin.y = last.basisOrigin[1];
            out.newBasisDirections = last.basisDirections;
        }
        else if (!last.newBasisDirections.empty() && last.newBasisOrigin.size() >= 2)
        {
            out.hasNewBasis = true;
            out.newBasisOrigin.x = last.newBasisOrigin[0];
            out.newBasisOrigin.y = last.newBasisOrigin[1];
            out.newBasisDirections = last.newBasisDirections;
        }
    }
    else
    {
        out.hasRow = false;
        out.k = result.trace[0].k;
        out.j = 0;
    }
}

// ----------------------------------------------------------------------------
// ФУНКЦИЯ FORMATPOINT
// ----------------------------------------------------------------------------
// Назначение:
// Преобразовать 2D-точку в компактную строку для нижней статусной панели.
// ----------------------------------------------------------------------------
static std::string FormatPoint(const Point2D& p)
{
    // ostringstream позволяет собрать подпись в привычном потоковом стиле.
    std::ostringstream out;

    // std::fixed и setprecision(4) фиксируют единый формат координат.
    out << "(" << std::fixed << std::setprecision(4) << p.x << ", " << p.y << ")";

    // str() возвращает накопленную строку.
    return out.str();
}

// ----------------------------------------------------------------------------
// КЛАСС INTERACTIVECHARTVIEW
// ----------------------------------------------------------------------------
// Назначение:
// Расширить стандартный QChartView поддержкой:
// 1) панорамирования мышью и тачпадом;
// 2) управляемого масштабирования;
// 3) пользовательской отрисовки осей, изолиний и локального базиса.
// ----------------------------------------------------------------------------
class InteractiveChartView : public QChartView
{
public:
    InteractiveChartView(QChart* chart,
                         const IObjectiveFunction* function,
                         QValueAxis* axisX,
                         QValueAxis* axisY,
                         const PlotBounds2D& initialBounds,
                         QWidget* parent)
        : QChartView(chart, parent),
          function_(function),
          axisX_(axisX),
          axisY_(axisY),
          initialBounds_(initialBounds),
          hasActiveSegment_(false),
          activeStyle_(Qt::SolidLine),
          panning_(false),
          hasContourCoverage_(false),
          contourGridSize_(0),
          contourLevelCount_(0),
          nativeZoomAccum_(0.0),
          hasActiveNativeGesture_(false),
          nativePinchInProgress_(false),
          xTickStep_(0.0),
          yTickStep_(0.0),
          hasNewBasis_(false)
    {
        // Включаем сглаживание, чтобы линии и точки выглядели аккуратно.
        setRenderHint(QPainter::Antialiasing, true);

        // Собственный drag-режим QGraphicsView нам не нужен: панорамирование
        // реализовано вручную через мышь и тачпад.
        setDragMode(QGraphicsView::NoDrag);
        setRubberBand(QChartView::NoRubberBand);

        // Отслеживание мыши без зажатой кнопки полезно для предсказуемой
        // обработки жестов и перерисовки.
        setMouseTracking(true);

        // MinimalViewportUpdate уменьшает цену частых перерисовок при движении.
        setViewportUpdateMode(QGraphicsView::MinimalViewportUpdate);

        // Разрешаем touch-события и самому view, и его viewport.
        setAttribute(Qt::WA_AcceptTouchEvents, true);
        viewport()->setAttribute(Qt::WA_AcceptTouchEvents, true);

        // Регистрируем pinch-жест у виджета и его viewport.
        grabGesture(Qt::PinchGesture);
        viewport()->grabGesture(Qt::PinchGesture);

        // Сразу подготавливаем разметку осей.
        RefreshAxesTicker();

        // И сразу строим начальные изолинии.
        UpdateContoursIfNeeded(true);
    }

    void SetFailedSegments(const std::vector<LineSegment>& s1,
                           const std::vector<LineSegment>& s2)
    {
        // Полностью заменяем текущие неудачные сегменты по обеим осям.
        failedS1Segments_ = s1;
        failedS2Segments_ = s2;

        // viewport()->update() просит Qt перерисовать график без полной
        // пересборки окна.
        viewport()->update();
    }

    void SetDirectionalSegments(const std::vector<LineSegment>& s1,
                                const std::vector<LineSegment>& s2)
    {
        // Сохраняем уже принятые успешные сегменты по направлениям s1 и s2.
        s1Segments_ = s1;
        s2Segments_ = s2;
        viewport()->update();
    }

    void SetActiveSegment(const LineSegment& segment, const QColor& color, Qt::PenStyle style)
    {
        // activeSegment_ — шаг, который надо выделить поверх остальных.
        activeSegment_ = segment;
        activeColor_ = color;
        activeStyle_ = style;
        hasActiveSegment_ = true;
        viewport()->update();
    }

    void ClearActiveSegment()
    {
        // Снимаем флаг показа активного шага.
        hasActiveSegment_ = false;
        viewport()->update();
    }

    void SetNewBasis(const Point2D& origin, const std::vector<Vector>& directions)
    {
        // origin — опорная точка маленького inset-рисунка базиса.
        newBasisOrigin_ = origin;

        // directions — набор направлений, который надо показать в inset.
        newBasisDirections_ = directions;

        // Показ inset имеет смысл только при непустом наборе направлений.
        hasNewBasis_ = !newBasisDirections_.empty();
        viewport()->update();
    }

    void ClearNewBasis()
    {
        // Полностью убираем inset с базисом.
        hasNewBasis_ = false;
        newBasisDirections_.clear();
        viewport()->update();
    }

    void ResetView()
    {
        // Ось X возвращаем к исходным границам просмотра.
        if (axisX_ != NULL)
        {
            axisX_->setRange(initialBounds_.xMin, initialBounds_.xMax);
        }

        // Ось Y тоже возвращаем к исходным границам.
        if (axisY_ != NULL)
        {
            axisY_->setRange(initialBounds_.yMin, initialBounds_.yMax);
        }

        // После сброса заново выравниваем масштаб по двум осям.
        EnforceEqualScale();

        // Сбрасываем флаги активных жестов, чтобы не осталось "подвешенного"
        // состояния после touchpad/pinch.
        hasActiveNativeGesture_ = false;
        nativePinchInProgress_ = false;

        // Перестраиваем подписи осей под новый диапазон.
        RefreshAxesTicker();

        // И насильно пересчитываем изолинии для исходного окна.
        UpdateContoursIfNeeded(true);
        viewport()->update();
    }

    void ZoomInSimple()
    {
        // 1.12 дает мягкий дискретный шаг приближения.
        ZoomCentered(1.12);
    }

    void ZoomOutSimple()
    {
        // Обратный коэффициент дает симметричное отдаление.
        ZoomCentered(1.0 / 1.12);
    }

protected:
    bool viewportEvent(QEvent* event)
    {
        // NativeGesture обрабатываем первыми, чтобы тачпад не перехватывался
        // стандартным поведением QChartView.
        if (event != NULL && event->type() == QEvent::NativeGesture)
        {
            QNativeGestureEvent* nativeEvent = static_cast<QNativeGestureEvent*>(event);
            if (HandleNativeGesture(nativeEvent))
            {
                return true;
            }
        }

        // Обычные gesture-события Qt проверяем после native-жестов.
        if (event != NULL && event->type() == QEvent::Gesture)
        {
            QGestureEvent* gestureEvent = static_cast<QGestureEvent*>(event);
            if (HandleGesture(gestureEvent))
            {
                return true;
            }
        }
        return QChartView::viewportEvent(event);
    }

    bool event(QEvent* event)
    {
        // Базовый event(...) пока не переопределяем содержательно, но оставляем
        // точку расширения на будущее.
        return QChartView::event(event);
    }

    void wheelEvent(QWheelEvent* event)
    {
        // Защитная ветка на случай некорректного вызова без события.
        if (event == NULL)
        {
            QChartView::wheelEvent(event);
            return;
        }

        // pixelDelta характерен для тачпадов и дает более плавное смещение.
        QPoint pixelDelta = event->pixelDelta();
        if (!pixelDelta.isNull())
        {
            PanByPixels(QPoint(static_cast<int>(std::lround(pixelDelta.x() * 2.2)),
                               static_cast<int>(std::lround(pixelDelta.y() * 2.2))));
            event->accept();
            return;
        }

        // angleDelta чаще приходит от обычного колесика мыши.
        const QPoint angle = event->angleDelta();
        if (!angle.isNull())
        {
            const double steps = static_cast<double>(angle.y()) / 120.0;
            if (std::isfinite(steps) && std::fabs(steps) > 1e-12)
            {
                // Масштабирование колесом включаем только с Ctrl, чтобы не
                // конфликтовать с обычной прокруткой.
                if (event->modifiers() & Qt::ControlModifier)
                {
                    double factor = std::pow(1.12, steps);
                    factor = std::clamp(factor, 0.72, 1.38);
                    ZoomAt(event->position(), factor);
                    event->accept();
                    return;
                }
            }
        }

        QChartView::wheelEvent(event);
    }

    void mousePressEvent(QMouseEvent* event)
    {
        // Зажатие средней или левой кнопки переводит view в режим панорамирования.
        if (event != NULL && (event->button() == Qt::MiddleButton || event->button() == Qt::LeftButton))
        {
            panning_ = true;
            lastMousePos_ = event->pos();
            setCursor(Qt::ClosedHandCursor);
            event->accept();
            return;
        }

        QChartView::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event)
    {
        // Пока активен режим panning_, движение мыши сдвигает окно обзора.
        if (event != NULL && panning_)
        {
            const QPoint delta = event->pos() - lastMousePos_;
            lastMousePos_ = event->pos();
            PanByPixels(delta);
            event->accept();
            return;
        }

        QChartView::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event)
    {
        // Отпускание кнопки завершает режим панорамирования.
        if (event != NULL && panning_ &&
            (event->button() == Qt::MiddleButton || event->button() == Qt::LeftButton))
        {
            panning_ = false;
            unsetCursor();
            event->accept();
            return;
        }

        QChartView::mouseReleaseEvent(event);
    }

    void resizeEvent(QResizeEvent* event)
    {
        // Сначала даем базовому классу обновить геометрию plot area.
        QChartView::resizeEvent(event);

        // Затем выравниваем оси и, при необходимости, плотность изолиний.
        EnforceEqualScale();
        RefreshAxesTicker();
        UpdateContoursIfNeeded(false);
    }

    void drawForeground(QPainter* painter, const QRectF& rect)
    {
        // rect нужен базовому классу; сами ниже рисуем поверх готового plot area.
        QChartView::drawForeground(painter, rect);

        // Без painter/chart/осей вручную рисовать нечего.
        if (painter == NULL || chart() == NULL || axisX_ == NULL || axisY_ == NULL)
        {
            return;
        }

        // plot — фактическая область графика без внешних полей и заголовка.
        const QRectF plot = chart()->plotArea();
        if (plot.width() <= 1.0 || plot.height() <= 1.0)
        {
            return;
        }

        // save()/restore() гарантируют, что пользовательские pen/brush/clip не
        // испортят дальнейший пайплайн Qt Charts.
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setClipRect(plot.adjusted(-1.0, -1.0, 1.0, 1.0));

        const double xMin = axisX_->min();
        const double xMax = axisX_->max();
        const double yMin = axisY_->min();
        const double yMax = axisY_->max();

        QPen axisPen(QColor(55, 55, 65, 220));
        axisPen.setWidthF(1.5);
        painter->setPen(axisPen);
        painter->setBrush(QBrush(QColor(55, 55, 65, 220)));

        // Если ноль попадает в диапазон X, вручную рисуем вертикальную ось x2.
        if (xMin <= 0.0 && 0.0 <= xMax)
        {
            const QPointF a = MapDataToPixel(Point2D{0.0, yMin});
            const QPointF b = MapDataToPixel(Point2D{0.0, yMax});
            painter->drawLine(a, b);
            const double arrow = 7.0;
            QPolygonF yArrow;
            yArrow << QPointF(b.x(), b.y())
                   << QPointF(b.x() - arrow * 0.55, b.y() + arrow)
                   << QPointF(b.x() + arrow * 0.55, b.y() + arrow);
            painter->drawPolygon(yArrow);
            painter->setPen(QColor(40, 40, 50, 230));
            painter->drawText(QPointF(b.x() + 6.0, b.y() + 14.0), "x2");
            painter->setPen(axisPen);
        }

        // Если ноль попадает в диапазон Y, вручную рисуем горизонтальную ось x1.
        if (yMin <= 0.0 && 0.0 <= yMax)
        {
            const QPointF a = MapDataToPixel(Point2D{xMin, 0.0});
            const QPointF b = MapDataToPixel(Point2D{xMax, 0.0});
            painter->drawLine(a, b);
            const double arrow = 7.0;
            QPolygonF xArrow;
            xArrow << QPointF(b.x(), b.y())
                   << QPointF(b.x() - arrow, b.y() - arrow * 0.55)
                   << QPointF(b.x() - arrow, b.y() + arrow * 0.55);
            painter->drawPolygon(xArrow);
            painter->setPen(QColor(40, 40, 50, 230));
            painter->drawText(QPointF(b.x() - 18.0, b.y() - 6.0), "x1");
            painter->setPen(axisPen);
        }

        std::size_t i = 0;
        // contourSegments_ уже содержит готовые сегменты всех уровней.
        for (i = 0; i < contourSegments_.size(); ++i)
        {
            QPen pen(contourSegments_[i].color);
            pen.setWidthF(1.25);
            pen.setStyle(Qt::SolidLine);
            painter->setPen(pen);
            painter->drawLine(MapDataToPixel(contourSegments_[i].from),
                              MapDataToPixel(contourSegments_[i].to));
        }

        QPen failedS1Pen(QColor(210, 55, 55, 185));
        failedS1Pen.setWidthF(1.9);
        failedS1Pen.setStyle(Qt::DashLine);
        painter->setPen(failedS1Pen);
        // failedS1Segments_ рисуем красным пунктиром.
        for (i = 0; i < failedS1Segments_.size(); ++i)
        {
            painter->drawLine(MapDataToPixel(failedS1Segments_[i].from),
                              MapDataToPixel(failedS1Segments_[i].to));
        }

        QPen failedS2Pen(QColor(210, 55, 55, 185));
        failedS2Pen.setWidthF(1.9);
        failedS2Pen.setStyle(Qt::DashLine);
        painter->setPen(failedS2Pen);
        // failedS2Segments_ тоже рисуем красным пунктиром, как неудачные попытки.
        for (i = 0; i < failedS2Segments_.size(); ++i)
        {
            painter->drawLine(MapDataToPixel(failedS2Segments_[i].from),
                              MapDataToPixel(failedS2Segments_[i].to));
        }

        // hasActiveSegment_ означает, что текущий шаг надо выделить отдельно.
        if (hasActiveSegment_)
        {
            painter->setRenderHint(QPainter::Antialiasing, true);
            QPen activePen(activeColor_);
            activePen.setWidthF(3.0);
            activePen.setStyle(activeStyle_);
            painter->setPen(activePen);
            painter->drawLine(MapDataToPixel(activeSegment_.from), MapDataToPixel(activeSegment_.to));
        }

        // В правом верхнем inset-окне показываем текущий базис направлений.
        if (hasNewBasis_ && !newBasisDirections_.empty())
        {
            const double xSpan = std::max(1e-12, std::fabs(axisX_->max() - axisX_->min()));
            const double ySpan = std::max(1e-12, std::fabs(axisY_->max() - axisY_->min()));
            const double sx = plot.width() / xSpan;
            const double sy = plot.height() / ySpan;

            // inset — прямоугольник маленького окна с базисом.
            const QRectF inset(plot.right() - 210.0, plot.top() + 10.0, 200.0, 138.0);
            painter->setPen(QPen(QColor(120, 125, 140, 180), 1.0));
            painter->setBrush(QColor(255, 255, 255, 215));
            painter->drawRoundedRect(inset, 8.0, 8.0);

            // inner — внутренняя область inset без рамки и внешних отступов.
            const QRectF inner = inset.adjusted(12.0, 12.0, -12.0, -12.0);

            // origin — визуальный центр, откуда выходят стрелки базиса.
            QPointF origin(inner.center().x(), inner.center().y() + 10.0);
            if (origin.y() > inner.bottom() - 4.0)
            {
                origin.setY(inner.bottom() - 4.0);
            }
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(50, 55, 65, 210));
            painter->drawEllipse(origin, 3.2, 3.2);

            // unitDirs хранит нормированные экранные направления базиса.
            std::vector<Point2D> unitDirs;

            // isValid позволяет пропустить вырожденные направления.
            std::vector<bool> isValid;
            unitDirs.resize(newBasisDirections_.size());
            isValid.resize(newBasisDirections_.size(), false);

            // minMaxLen — общая верхняя граница длины вектора, чтобы все стрелки
            // гарантированно помещались в inset.
            double minMaxLen = std::numeric_limits<double>::infinity();
            std::size_t bi = 0;
            for (bi = 0; bi < newBasisDirections_.size(); ++bi)
            {
                const Vector& dir = newBasisDirections_[bi];
                if (dir.size() < 2)
                {
                    continue;
                }

                // px и py — тот же вектор, но уже в пикселях plot area.
                double px = dir[0] * sx;
                double py = -dir[1] * sy;
                const double norm = std::sqrt(px * px + py * py);
                if (!(norm > 1e-12) || !std::isfinite(norm))
                {
                    continue;
                }

                // ux и uy — единичное направление в экранных координатах.
                const double ux = px / norm;
                const double uy = py / norm;
                unitDirs[bi].x = ux;
                unitDirs[bi].y = uy;
                isValid[bi] = true;

                // maxLen — максимальная длина, с которой данный вектор еще
                // останется внутри внутреннего прямоугольника inset.
                double maxLen = std::numeric_limits<double>::infinity();
                if (ux > 1e-12)
                {
                    maxLen = std::min(maxLen, (inner.right() - origin.x()) / ux);
                }
                else if (ux < -1e-12)
                {
                    maxLen = std::min(maxLen, (inner.left() - origin.x()) / ux);
                }
                if (uy > 1e-12)
                {
                    maxLen = std::min(maxLen, (inner.bottom() - origin.y()) / uy);
                }
                else if (uy < -1e-12)
                {
                    maxLen = std::min(maxLen, (inner.top() - origin.y()) / uy);
                }

                if (std::isfinite(maxLen) && maxLen > 1.0)
                {
                    minMaxLen = std::min(minMaxLen, maxLen);
                }
            }

            // basisLenPx — итоговая длина стрелок в inset в пикселях.
            double basisLenPx = 40.0;
            if (std::isfinite(minMaxLen) && minMaxLen > 1.0)
            {
                basisLenPx = std::min(56.0, minMaxLen * 0.86);
                basisLenPx = std::max(10.0, basisLenPx);
            }

            // Рисуем каждую допустимую ось базиса.
            for (bi = 0; bi < newBasisDirections_.size(); ++bi)
            {
                if (!isValid[bi])
                {
                    continue;
                }

                // Масштабируем единичное направление до итоговой длины.
                const double px = basisLenPx * unitDirs[bi].x;
                const double py = basisLenPx * unitDirs[bi].y;
                const QPointF to(origin.x() + px, origin.y() + py);

                // Чередуем цвета осей s1/s2/s3 по принятой в приложении схеме.
                QColor basisColor = (bi % 2 == 0) ? QColor(36, 121, 255, 210) : QColor(10, 170, 120, 210);
                QPen basisPen(basisColor);
                basisPen.setWidthF(2.2);
                basisPen.setStyle(Qt::DashLine);
                painter->setPen(basisPen);
                painter->drawLine(origin, to);

                // Подпись оси стараемся держать внутри inset-окна.
                painter->setPen(QColor(30, 30, 30, 235));
                QPointF labelPos = to + QPointF(4.0, -4.0);
                const double labelMinX = inset.left() + 4.0;
                const double labelMaxX = inset.right() - 22.0;
                const double labelMinY = inset.top() + 12.0;
                const double labelMaxY = inset.bottom() - 4.0;
                if (labelPos.x() < labelMinX)
                {
                    labelPos.setX(labelMinX);
                }
                else if (labelPos.x() > labelMaxX)
                {
                    labelPos.setX(labelMaxX);
                }
                if (labelPos.y() < labelMinY)
                {
                    labelPos.setY(labelMinY);
                }
                else if (labelPos.y() > labelMaxY)
                {
                    labelPos.setY(labelMaxY);
                }
                painter->drawText(labelPos, QString::fromUtf8("s") + QString::number(static_cast<int>(bi + 1)));
            }
        }

        // Удачные шаги по первому направлению рисуем синим.
        QPen s1Pen(QColor(36, 121, 255, 215));
        s1Pen.setWidthF(2.4);
        s1Pen.setStyle(Qt::SolidLine);
        painter->setPen(s1Pen);
        for (i = 0; i < s1Segments_.size(); ++i)
        {
            painter->drawLine(MapDataToPixel(s1Segments_[i].from),
                              MapDataToPixel(s1Segments_[i].to));
        }

        // Удачные шаги по второму направлению рисуем зеленым.
        QPen s2Pen(QColor(10, 170, 120, 215));
        s2Pen.setWidthF(2.4);
        s2Pen.setStyle(Qt::SolidLine);
        painter->setPen(s2Pen);
        for (i = 0; i < s2Segments_.size(); ++i)
        {
            painter->drawLine(MapDataToPixel(s2Segments_[i].from),
                              MapDataToPixel(s2Segments_[i].to));
        }

        // Восстанавливаем состояние painter, сохраненное в начале функции.
        painter->restore();
    }

private:
    bool HandleNativeGesture(QNativeGestureEvent* event)
    {
        // Защита от некорректного вызова без события.
        if (event == NULL)
        {
            return false;
        }

        // type определяет конкретный тип native-жеста от системы.
        const Qt::NativeGestureType type = event->gestureType();
        if (type == Qt::BeginNativeGesture)
        {
            nativeZoomAccum_ = 0.0;
            hasActiveNativeGesture_ = true;
            nativePinchInProgress_ = false;
            return true;
        }

        if (type == Qt::EndNativeGesture)
        {
            nativeZoomAccum_ = 0.0;
            hasActiveNativeGesture_ = false;
            if (nativePinchInProgress_)
            {
                UpdateContoursIfNeeded(false);
                viewport()->update();
            }
            nativePinchInProgress_ = false;
            return true;
        }

        if (type == Qt::ZoomNativeGesture || type == Qt::SmartZoomNativeGesture)
        {
            // Тачпад-зум отключен по требованию пользователя.
            return true;
        }

        if (type == Qt::PanNativeGesture || type == Qt::SwipeNativeGesture)
        {
            if (nativePinchInProgress_)
            {
                return true;
            }

            // d — экранное смещение, пришедшее от жеста.
            QPointF d = event->delta();
            if (!std::isfinite(d.x()) || !std::isfinite(d.y()))
            {
                return true;
            }

            // panPixels усиливает естественный жест, чтобы прокрутка ощущалась
            // внятно на больших экранах.
            QPoint panPixels(static_cast<int>(std::lround(d.x() * 2.8)),
                             static_cast<int>(std::lround(d.y() * 2.8)));
            if (!panPixels.isNull())
            {
                PanByPixels(panPixels);
            }
            return true;
        }

        return false;
    }

    bool HandleGesture(QGestureEvent* event)
    {
        if (event == NULL)
        {
            return false;
        }

#if defined(__APPLE__)
        // На macOS pinch-жест отключен (зум только кнопками и Ctrl+колесо).
        return false;
#endif

        QGesture* gesture = event->gesture(Qt::PinchGesture);
        if (gesture == NULL)
        {
            return false;
        }

        if (gesture->state() == Qt::GestureFinished || gesture->state() == Qt::GestureCanceled)
        {
            UpdateContoursIfNeeded(false);
            viewport()->update();
        }

        // pinch — конкретный жест масштабирования.
        QPinchGesture* pinch = static_cast<QPinchGesture*>(gesture);
        if (pinch->changeFlags() & QPinchGesture::ScaleFactorChanged)
        {
            const double ratio = pinch->scaleFactor() / pinch->lastScaleFactor();
            if (std::isfinite(ratio) && std::fabs(ratio - 1.0) > 1e-6)
            {
                double factor = std::clamp(ratio, 0.78, 1.30);
                QPointF center = pinch->centerPoint();
                if (!std::isfinite(center.x()) || !std::isfinite(center.y()))
                {
                    center = chart() != NULL ? chart()->plotArea().center() : QPointF(0.0, 0.0);
                }
                ZoomAt(center, factor);
            }
            event->accept(gesture);
            return true;
        }

        return false;
    }

    QPointF MapDataToPixel(const Point2D& data) const
    {
        // out инициализируем нулями на случай любого раннего выхода.
        QPointF out(0.0, 0.0);

        if (chart() == NULL || axisX_ == NULL || axisY_ == NULL)
        {
            return out;
        }

        const QRectF plot = chart()->plotArea();
        const double xMin = axisX_->min();
        const double xMax = axisX_->max();
        const double yMin = axisY_->min();
        const double yMax = axisY_->max();

        if (!(xMax > xMin) || !(yMax > yMin))
        {
            return out;
        }

        // rx и ry — нормированные координаты точки внутри текущего диапазона.
        const double rx = (data.x - xMin) / (xMax - xMin);
        const double ry = (data.y - yMin) / (yMax - yMin);

        // Переводим нормированные координаты в пиксели plot area.
        out.setX(plot.left() + rx * plot.width());
        out.setY(plot.bottom() - ry * plot.height());
        return out;
    }

    bool MapPixelToData(const QPointF& pixel, Point2D& outData) const
    {
        if (chart() == NULL || axisX_ == NULL || axisY_ == NULL)
        {
            return false;
        }

        const QRectF plot = chart()->plotArea();
        if (plot.width() <= 1.0 || plot.height() <= 1.0)
        {
            return false;
        }

        const double xMin = axisX_->min();
        const double xMax = axisX_->max();
        const double yMin = axisY_->min();
        const double yMax = axisY_->max();

        if (!(xMax > xMin) || !(yMax > yMin))
        {
            return false;
        }

        // rx и ry — нормированные координаты пикселя внутри plot area.
        double rx = (pixel.x() - plot.left()) / plot.width();
        double ry = (plot.bottom() - pixel.y()) / plot.height();

        if (rx < 0.0)
        {
            rx = 0.0;
        }
        if (rx > 1.0)
        {
            rx = 1.0;
        }
        if (ry < 0.0)
        {
            ry = 0.0;
        }
        if (ry > 1.0)
        {
            ry = 1.0;
        }

        // Восстанавливаем реальные координаты в пользовательском пространстве.
        outData.x = xMin + rx * (xMax - xMin);
        outData.y = yMin + ry * (yMax - yMin);
        return true;
    }

    void ZoomCentered(double factor)
    {
        if (chart() == NULL)
        {
            return;
        }
        const QPointF center = chart()->plotArea().center();
        ZoomAt(center, factor);
    }

    void ZoomAt(const QPointF& pixelPos, double factor)
    {
        if (axisX_ == NULL || axisY_ == NULL || factor <= 0.0 || !std::isfinite(factor))
        {
            return;
        }

        const double xMin = axisX_->min();
        const double xMax = axisX_->max();
        const double yMin = axisY_->min();
        const double yMax = axisY_->max();

        if (!(xMax > xMin) || !(yMax > yMin))
        {
            return;
        }

        // anchor — точка в координатах данных, вокруг которой масштабируем.
        Point2D anchor;
        if (!MapPixelToData(pixelPos, anchor))
        {
            anchor.x = 0.5 * (xMin + xMax);
            anchor.y = 0.5 * (yMin + yMax);
        }

        // Новые границы считаются так, чтобы anchor остался под тем же пикселем.
        const double newXMin = anchor.x - (anchor.x - xMin) / factor;
        const double newXMax = anchor.x + (xMax - anchor.x) / factor;
        const double newYMin = anchor.y - (anchor.y - yMin) / factor;
        const double newYMax = anchor.y + (yMax - anchor.y) / factor;

        if (std::fabs(newXMax - newXMin) < 1e-10 || std::fabs(newYMax - newYMin) < 1e-10)
        {
            return;
        }

        axisX_->setRange(newXMin, newXMax);
        axisY_->setRange(newYMin, newYMax);
        EnforceEqualScale();
        RefreshAxesTicker();
        if (!hasActiveNativeGesture_ && !nativePinchInProgress_)
        {
            UpdateContoursIfNeeded(false);
        }
        viewport()->update();
    }

    void PanByPixels(const QPoint& pixelDelta)
    {
        if (axisX_ == NULL || axisY_ == NULL || chart() == NULL)
        {
            return;
        }

        const QRectF plot = chart()->plotArea();
        if (plot.width() <= 1.0 || plot.height() <= 1.0)
        {
            return;
        }

        const double xMin = axisX_->min();
        const double xMax = axisX_->max();
        const double yMin = axisY_->min();
        const double yMax = axisY_->max();

        // panBoost делает жест панорамирования немного энергичнее.
        const double panBoost = 1.35;
        const double dx = -panBoost * static_cast<double>(pixelDelta.x()) / plot.width() * (xMax - xMin);
        const double dy = panBoost * static_cast<double>(pixelDelta.y()) / plot.height() * (yMax - yMin);

        axisX_->setRange(xMin + dx, xMax + dx);
        axisY_->setRange(yMin + dy, yMax + dy);
        EnforceEqualScale();
        RefreshAxesTicker();
        UpdateContoursIfNeeded(false);
        viewport()->update();
    }

    void EnforceEqualScale()
    {
        if (axisX_ == NULL || axisY_ == NULL || chart() == NULL)
        {
            return;
        }

        const QRectF plot = chart()->plotArea();
        if (plot.width() <= 1.0 || plot.height() <= 1.0)
        {
            return;
        }

        const double xMin = axisX_->min();
        const double xMax = axisX_->max();
        const double yMin = axisY_->min();
        const double yMax = axisY_->max();

        if (!(xMax > xMin) || !(yMax > yMin))
        {
            return;
        }

        const double xSpan = xMax - xMin;
        const double ySpan = yMax - yMin;
        const double xPerPixel = xSpan / plot.width();
        const double yPerPixel = ySpan / plot.height();

        if (!std::isfinite(xPerPixel) || !std::isfinite(yPerPixel) || !(xPerPixel > 0.0) ||
            !(yPerPixel > 0.0))
        {
            return;
        }

        // reference нужен для относительного сравнения двух масштабов.
        const double reference = std::max(xPerPixel, yPerPixel);
        if (std::fabs(xPerPixel - yPerPixel) <= reference * 1e-6)
        {
            return;
        }

        // centerX и centerY — текущий центр видимого окна.
        const double centerX = 0.5 * (xMin + xMax);
        const double centerY = 0.5 * (yMin + yMax);

        // Расширяем ту ось, которая оказалась "слишком плотной", чтобы единица
        // по X и Y отображалась одинаково по пикселям.
        if (xPerPixel > yPerPixel)
        {
            const double newYSpan = xPerPixel * plot.height();
            axisY_->setRange(centerY - 0.5 * newYSpan, centerY + 0.5 * newYSpan);
        }
        else
        {
            const double newXSpan = yPerPixel * plot.width();
            axisX_->setRange(centerX - 0.5 * newXSpan, centerX + 0.5 * newXSpan);
        }
    }

    void RefreshAxesTicker()
    {
        if (axisX_ == NULL || axisY_ == NULL || chart() == NULL)
        {
            return;
        }

        const QRectF plot = chart()->plotArea();
        // targetX/targetY — целевое число крупных подписей по осям.
        int targetX = static_cast<int>(std::floor(plot.width() / 84.0));
        int targetY = static_cast<int>(std::floor(plot.height() / 66.0));
        targetX = std::clamp(targetX, 8, 12);
        targetY = std::clamp(targetY, 8, 12);

        // xSpan и ySpan — текущие численные диапазоны осей.
        const double xSpan = std::fabs(axisX_->max() - axisX_->min());
        const double ySpan = std::fabs(axisY_->max() - axisY_->min());
        xTickStep_ = StableTickStep(xTickStep_, xSpan, targetX);
        yTickStep_ = StableTickStep(yTickStep_, ySpan, targetY);
        if (!(xTickStep_ > 0.0) || !std::isfinite(xTickStep_))
        {
            xTickStep_ = 1.0;
        }
        if (!(yTickStep_ > 0.0) || !std::isfinite(yTickStep_))
        {
            yTickStep_ = 1.0;
        }

        // xDecimals/yDecimals определяют формат вывода подписей.
        const int xDecimals = DecimalsForStep(xTickStep_);
        const int yDecimals = DecimalsForStep(yTickStep_);

        // Включаем динамические тики с шагом, найденным выше.
        axisX_->setTickType(QValueAxis::TicksDynamic);
        axisY_->setTickType(QValueAxis::TicksDynamic);
        axisX_->setTickAnchor(0.0);
        axisY_->setTickAnchor(0.0);
        axisX_->setTickInterval(xTickStep_);
        axisY_->setTickInterval(yTickStep_);
        axisX_->setMinorTickCount(3);
        axisY_->setMinorTickCount(3);
        axisX_->setLabelFormat(QString::fromStdString("%." + std::to_string(xDecimals) + "f"));
        axisY_->setLabelFormat(QString::fromStdString("%." + std::to_string(yDecimals) + "f"));
        axisX_->setTruncateLabels(false);
        axisY_->setTruncateLabels(false);
    }

    static bool ContainsBounds(const PlotBounds2D& outer, const PlotBounds2D& inner, double marginFactor)
    {
        // dx и dy — размеры внешней области.
        const double dx = outer.xMax - outer.xMin;
        const double dy = outer.yMax - outer.yMin;

        // mx и my — защитные внутренние отступы.
        const double mx = std::max(1e-9, dx * marginFactor);
        const double my = std::max(1e-9, dy * marginFactor);

        return (inner.xMin >= outer.xMin + mx) && (inner.xMax <= outer.xMax - mx) &&
               (inner.yMin >= outer.yMin + my) && (inner.yMax <= outer.yMax - my);
    }

    static double BoundsArea(const PlotBounds2D& b)
    {
        // Даже для вырожденных диапазонов подставляем маленький положительный
        // минимум, чтобы не делить на ноль позже.
        const double dx = std::max(1e-12, std::fabs(b.xMax - b.xMin));
        const double dy = std::max(1e-12, std::fabs(b.yMax - b.yMin));
        return dx * dy;
    }

    PlotBounds2D CurrentExpandedBounds() const
    {
        // b начинает как точная текущая видимая область осей.
        PlotBounds2D b;
        b.xMin = axisX_->min();
        b.xMax = axisX_->max();
        b.yMin = axisY_->min();
        b.yMax = axisY_->max();

        // Добавляем внешний запас, чтобы изолинии строились чуть шире экрана и
        // не "обрывались" по краям при панорамировании.
        const double dx = b.xMax - b.xMin;
        const double dy = b.yMax - b.yMin;
        const double padX = std::max(0.15, dx * 0.28);
        const double padY = std::max(0.15, dy * 0.28);
        b.xMin -= padX;
        b.xMax += padX;
        b.yMin -= padY;
        b.yMax += padY;
        return b;
    }

    void ComputeAdaptiveContourDetail(std::size_t& gridSize, std::size_t& levelCount) const
    {
        // minPixels — характерный линейный размер plot area.
        double minPixels = 900.0;
        if (chart() != NULL)
        {
            const QRectF plot = chart()->plotArea();
            if (plot.width() > 1.0 && plot.height() > 1.0 && std::isfinite(plot.width()) &&
                std::isfinite(plot.height()))
            {
                minPixels = std::min(plot.width(), plot.height());
            }
        }

        // baseGrid/baseLevels — базовая плотность изолиний без учета масштаба.
        const double baseGrid =
            std::clamp(minPixels / 8.4, static_cast<double>(CONTOUR_GRID_MIN), 118.0);
        const double baseLevels =
            std::clamp(minPixels / 8.0, static_cast<double>(CONTOUR_LEVEL_MIN), 116.0);

        const double initDx = std::max(1e-9, std::fabs(initialBounds_.xMax - initialBounds_.xMin));
        const double initDy = std::max(1e-9, std::fabs(initialBounds_.yMax - initialBounds_.yMin));
        const double curDx = std::max(1e-9, std::fabs(axisX_->max() - axisX_->min()));
        const double curDy = std::max(1e-9, std::fabs(axisY_->max() - axisY_->min()));
        const double areaRatio = (initDx * initDy) / (curDx * curDy);
        // zoomFactor показывает, во сколько раз текущее окно меньше исходного.
        double zoomFactor = std::sqrt(std::max(1e-9, areaRatio));
        if (!std::isfinite(zoomFactor) || zoomFactor < 1.0)
        {
            zoomFactor = 1.0;
        }
        if (zoomFactor > 30.0)
        {
            zoomFactor = 30.0;
        }

        // zoomBoost переводит масштаб в более мягкую логарифмическую шкалу.
        double zoomBoost = std::log2(zoomFactor);
        if (!std::isfinite(zoomBoost) || zoomBoost < 0.0)
        {
            zoomBoost = 0.0;
        }

        const double adaptiveGrid = baseGrid + zoomBoost * 9.0;
        const double adaptiveLevels = baseLevels + zoomBoost * 14.0;

        gridSize = static_cast<std::size_t>(std::lround(adaptiveGrid));
        levelCount = static_cast<std::size_t>(std::lround(adaptiveLevels));
        gridSize = std::clamp(gridSize, CONTOUR_GRID_MIN, CONTOUR_GRID_MAX);
        levelCount = std::clamp(levelCount, CONTOUR_LEVEL_MIN, CONTOUR_LEVEL_MAX);
    }

    void UpdateContoursIfNeeded(bool force)
    {
        if (function_ == NULL || axisX_ == NULL || axisY_ == NULL)
        {
            return;
        }

        // requested — область, для которой сейчас нужны изолинии.
        PlotBounds2D requested = CurrentExpandedBounds();
        std::size_t wantedGrid = 0;
        std::size_t wantedLevels = 0;
        ComputeAdaptiveContourDetail(wantedGrid, wantedLevels);

        // Если текущего кэша изолиний достаточно, пересчет можно пропустить.
        if (!force && hasContourCoverage_ && ContainsBounds(contourCoverage_, requested, 0.04) &&
            contourGridSize_ >= wantedGrid && contourLevelCount_ >= wantedLevels)
        {
            const double coverageArea = BoundsArea(contourCoverage_);
            const double requestArea = BoundsArea(requested);
            const double zoomInRatio = coverageArea / requestArea;

            // Если приближение заметное, пересчитываем изолинии заново,
            // даже при достаточной прошлой сетке.
            if (std::isfinite(zoomInRatio) && zoomInRatio < 1.35)
            {
                return;
            }
        }

        // grid получит новую регулярную сетку значений функции.
        ContourGrid grid;
        std::string contourError;
        if (!BuildContourGrid(function_, requested, wantedGrid, grid, contourError))
        {
            return;
        }

        // adaptiveLevels — уровни изолиний на новом диапазоне.
        const std::vector<double> adaptiveLevels = BuildContourLevels(grid.minZ, grid.maxZ, wantedLevels);

        // newSegments — новый набор сегментов изолиний.
        std::vector<ContourSegment> newSegments;
        BuildContourSegmentsFromGrid(grid, adaptiveLevels, newSegments);

        // swap дешевле копирования большого массива сегментов.
        contourSegments_.swap(newSegments);
        contourCoverage_ = requested;
        hasContourCoverage_ = true;
        contourGridSize_ = wantedGrid;
        contourLevelCount_ = wantedLevels;
    }

private:
    // function_ — целевая функция для пересчета f(x) в статусной строке.
    const IObjectiveFunction* function_;

    // axisX_ — нижняя численная ось графика.
    QValueAxis* axisX_;

    // axisY_ — левая численная ось графика.
    QValueAxis* axisY_;

    // initialBounds_ — диапазон, к которому ResetView() возвращает график.
    PlotBounds2D initialBounds_;

    // contourCoverage_ — область, для которой уже построены текущие изолинии.
    PlotBounds2D contourCoverage_;

    // hasContourCoverage_ показывает, инициализирован ли contourCoverage_.
    bool hasContourCoverage_;

    // contourGridSize_ — размер сетки, на которой считались изолинии.
    std::size_t contourGridSize_;

    // contourLevelCount_ — число уровней изолиний в текущем кэше.
    std::size_t contourLevelCount_;

    // contourSegments_ — уже готовые сегменты изолиний для drawForeground(...).
    std::vector<ContourSegment> contourSegments_;

    // failedS1Segments_ — неудачные попытки по первому направлению.
    std::vector<LineSegment> failedS1Segments_;

    // failedS2Segments_ — неудачные попытки по второму направлению.
    std::vector<LineSegment> failedS2Segments_;

    // s1Segments_ — удачные уже принятые шаги вдоль первого направления.
    std::vector<LineSegment> s1Segments_;

    // s2Segments_ — удачные уже принятые шаги вдоль второго направления.
    std::vector<LineSegment> s2Segments_;

    // activeSegment_ — текущий подсвечиваемый шаг.
    LineSegment activeSegment_;

    // hasActiveSegment_ говорит, нужно ли его сейчас рисовать.
    bool hasActiveSegment_;

    // activeColor_ — цвет выделения текущего шага.
    QColor activeColor_;

    // activeStyle_ — стиль пера для текущего шага.
    Qt::PenStyle activeStyle_;

    // hasNewBasis_ показывает, нужно ли рисовать inset с базисом.
    bool hasNewBasis_;

    // newBasisOrigin_ — опорная точка для текущего набора направлений.
    Point2D newBasisOrigin_;

    // newBasisDirections_ — сами направления базиса для inset-окна.
    std::vector<Vector> newBasisDirections_;

    // panning_ показывает, активен ли сейчас режим панорамирования мышью.
    bool panning_;

    // lastMousePos_ хранит предыдущую позицию мыши для вычисления смещения.
    QPoint lastMousePos_;

    // nativeZoomAccum_ накапливает масштаб из native trackpad-жеста.
    double nativeZoomAccum_;

    // hasActiveNativeGesture_ показывает, что native gesture уже начат.
    bool hasActiveNativeGesture_;

    // nativePinchInProgress_ отличает pinch от обычной прокрутки.
    bool nativePinchInProgress_;

    // xTickStep_ — текущий шаг крупных делений по оси X.
    double xTickStep_;

    // yTickStep_ — текущий шаг крупных делений по оси Y.
    double yTickStep_;
};

// ----------------------------------------------------------------------------
// КЛАСС ROSENBROCKVIEWERWIDGET
// ----------------------------------------------------------------------------
// Назначение:
// Собрать полноценное окно 2D-визуализации: график, панель управления
// анимацией, слайдеры, статусную строку и связь с таблицей шагов.
// ----------------------------------------------------------------------------
class RosenbrockViewerWidget : public QWidget
{
public:
    RosenbrockViewerWidget(const IObjectiveFunction* function,
                           const OptimizationResult& result,
                           const PlotBounds2D& bounds,
                           std::size_t kMax,
                           std::size_t initialStep,
                           const ViewerStepChangedCallback& onStepChanged,
                           QWidget* parent)
        : QWidget(parent),
          // function_ — целевая функция, связанная с текущим запуском метода.
          function_(function),
          // result_ — полная трассировка и итоги оптимизации.
          result_(result),
          // bounds_ — стартовый диапазон координатной плоскости для графика.
          bounds_(bounds),
          // kMax_ — максимальный номер внешней итерации среди всех trace-строк.
          kMax_(kMax),
          // stepMax_ — число шагов показа, равное длине трассы.
          stepMax_(result.trace.size()),
          // stepShown_ — стартовый индекс показываемого шага.
          stepShown_((initialStep == std::numeric_limits<std::size_t>::max() ||
                      initialStep > result.trace.size())
                         ? result.trace.size()
                         : initialStep),
          // ready_ поднимется в true только в конце успешной инициализации.
          ready_(false),
          // onStepChanged_ — внешний callback синхронизации с таблицей.
          onStepChanged_(onStepChanged)
    {
        // Настраиваем базовые свойства окна визуализации.
        setWindowTitle(QString::fromUtf8("Rosenbrock 2D Viewer (Qt Charts)"));
        // Минимальный размер нужен, чтобы панели управления не схлопывались.
        setMinimumSize(1220, 800);
        // StrongFocus позволяет окну принимать клавиатурные шорткаты.
        setFocusPolicy(Qt::StrongFocus);
        // objectName используется в QSS как селектор корневого виджета.
        setObjectName("viewerRoot");

        // Единая темная тема верхней и нижней панелей управления.
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

        // root — главная вертикальная раскладка окна: верхняя панель,
        // график и нижняя панель.
        QVBoxLayout* root = new QVBoxLayout(this);
        root->setContentsMargins(6, 6, 6, 6);
        root->setSpacing(6);

        // chart_ — объект Qt Charts, в который будут добавлены оси и серии.
        chart_ = new QChart();
        // Внутренняя тема графика остается светлой для лучшей читаемости изолиний.
        chart_->setTheme(QChart::ChartThemeLight);
        // Общий фон chart делаем белым.
        chart_->setBackgroundBrush(QBrush(Qt::white));
        // Разрешаем отдельный фон именно plot area.
        chart_->setPlotAreaBackgroundVisible(true);
        // Plot area чуть серее белого, чтобы сетка и контуры отделялись от фона.
        chart_->setPlotAreaBackgroundBrush(QColor(248, 248, 250));
        // Заголовок позже обновляется по текущим k и j.
        chart_->setTitle(QString::fromUtf8("Метод Розенброка (2D)"));
        // Анимации Qt Charts отключаем, потому что шаговая анимация у нас ручная.
        chart_->setAnimationOptions(QChart::NoAnimation);
        // Стандартную легенду скрываем: семантика цветов уже очевидна из рисунка.
        chart_->legend()->setVisible(false);
        // Выравнивание легенды оставляем как нейтральный дефолт.
        chart_->legend()->setAlignment(Qt::AlignTop);

        // axisX_ и axisY_ — численные оси графика.
        axisX_ = new QValueAxis();
        axisY_ = new QValueAxis();
        // Начальный диапазон X берем из bounds_.
        axisX_->setRange(bounds_.xMin, bounds_.xMax);
        // Начальный диапазон Y тоже берем из bounds_.
        axisY_->setRange(bounds_.yMin, bounds_.yMax);
        // Встроенные подписи осей не нужны, потому что x1/x2 рисуются вручную.
        axisX_->setTitleText("");
        axisY_->setTitleText("");
        // Dynamic ticks позволяют задавать собственный шаг и якорь сетки.
        axisX_->setTickType(QValueAxis::TicksDynamic);
        axisY_->setTickType(QValueAxis::TicksDynamic);
        // Якорим сетку в нуле, чтобы оси выглядели устойчиво при сдвиге.
        axisX_->setTickAnchor(0.0);
        axisY_->setTickAnchor(0.0);
        // Стартовый интервал будет позднее уточнен через RefreshAxesTicker().
        axisX_->setTickInterval(1.0);
        axisY_->setTickInterval(1.0);
        // Между основными делениями рисуем дополнительные minor ticks.
        axisX_->setMinorTickCount(3);
        axisY_->setMinorTickCount(3);
        // Начальный формат подписей — три знака после запятой.
        axisX_->setLabelFormat("%.3f");
        axisY_->setLabelFormat("%.3f");
        // Запрещаем усекать метки осей.
        axisX_->setTruncateLabels(false);
        axisY_->setTruncateLabels(false);
        // Цвет подписей подбираем мягким темно-синим.
        axisX_->setLabelsColor(QColor("#334155"));
        axisY_->setLabelsColor(QColor("#334155"));
        // Основная сетка рисуется чуть заметнее minor grid.
        axisX_->setGridLineColor(QColor("#d3dee9"));
        axisY_->setGridLineColor(QColor("#d3dee9"));
        // Minor grid делаем еще светлее.
        axisX_->setMinorGridLineColor(QColor("#e6edf5"));
        axisY_->setMinorGridLineColor(QColor("#e6edf5"));

        // Регистрируем оси в графике.
        chart_->addAxis(axisX_, Qt::AlignBottom);
        chart_->addAxis(axisY_, Qt::AlignLeft);

        // chartView_ — интерактивная обертка над графиком с пользовательской
        // отрисовкой и жестами.
        chartView_ = new InteractiveChartView(chart_, function_, axisX_, axisY_, bounds_, this);
        // Фокус удерживаем на виджете окна, а не на внутреннем chart view.
        chartView_->setFocusPolicy(Qt::NoFocus);

        // topPanel — верхняя панель управления пошаговой анимацией и масштабом.
        QWidget* topPanel = new QWidget(this);
        topPanel->setObjectName("viewerTopPanel");
        QHBoxLayout* controlsTop = new QHBoxLayout(topPanel);
        // Поля панели подбираем компактно, чтобы не отнимать высоту у графика.
        controlsTop->setContentsMargins(6, 4, 6, 4);
        controlsTop->setSpacing(4);

        // timelineGroup — группа кнопок навигации по шагам.
        QWidget* timelineGroup = new QWidget(topPanel);
        timelineGroup->setObjectName("viewerGroup");
        QHBoxLayout* timelineLayout = new QHBoxLayout(timelineGroup);
        // Внутри локальной группы не нужны дополнительные отступы по периметру.
        timelineLayout->setContentsMargins(0, 0, 0, 0);
        timelineLayout->setSpacing(3);
        QLabel* timelineLabel = new QLabel(QString::fromUtf8("Шаги"), topPanel);
        timelineLabel->setObjectName("panelTitle");

        // first/prev/play/next/last — стандартные кнопки перемещения по трассе.
        firstButton_ = new QPushButton(QString::fromUtf8("|<"), topPanel);
        prevButton_ = new QPushButton(QString::fromUtf8("<<"), topPanel);
        playPauseButton_ = new QPushButton(QString::fromUtf8("▶"), topPanel);
        nextButton_ = new QPushButton(QString::fromUtf8(">>"), topPanel);
        lastButton_ = new QPushButton(QString::fromUtf8(">|"), topPanel);
        firstButton_->setProperty("controlRole", "nav");
        prevButton_->setProperty("controlRole", "nav");
        nextButton_->setProperty("controlRole", "nav");
        lastButton_->setProperty("controlRole", "nav");
        // Кнопка play/pause визуально акцентная, поэтому для нее отдельная роль.
        playPauseButton_->setProperty("controlRole", "accent");
        timelineLayout->addWidget(timelineLabel);
        timelineLayout->addWidget(firstButton_);
        timelineLayout->addWidget(prevButton_);
        timelineLayout->addWidget(playPauseButton_);
        timelineLayout->addWidget(nextButton_);
        timelineLayout->addWidget(lastButton_);

        // sep1 визуально отделяет навигацию по шагам от группы масштаба.
        QFrame* sep1 = new QFrame(topPanel);
        sep1->setObjectName("lineSep");
        sep1->setFrameShape(QFrame::VLine);

        // zoomGroup — группа управления масштабом.
        QWidget* zoomGroup = new QWidget(topPanel);
        zoomGroup->setObjectName("viewerGroup");
        QHBoxLayout* zoomLayout = new QHBoxLayout(zoomGroup);
        zoomLayout->setContentsMargins(0, 0, 0, 0);
        zoomLayout->setSpacing(3);
        QLabel* zoomLabel = new QLabel(QString::fromUtf8("Масштаб"), topPanel);
        zoomLabel->setObjectName("panelTitle");
        zoomOutButton_ = new QPushButton(QString::fromUtf8("−"), topPanel);
        zoomInButton_ = new QPushButton(QString::fromUtf8("+"), topPanel);
        zoomOutButton_->setProperty("controlRole", "icon");
        zoomInButton_->setProperty("controlRole", "icon");
        zoomLayout->addWidget(zoomLabel);
        zoomLayout->addWidget(zoomOutButton_);
        zoomLayout->addWidget(zoomInButton_);

        // sep2 отделяет группу масштаба от правой служебной части панели.
        QFrame* sep2 = new QFrame(topPanel);
        sep2->setObjectName("lineSep");
        sep2->setFrameShape(QFrame::VLine);

        // iterationLabel_ показывает текущий шаг/итерацию в виде короткого статуса.
        iterationLabel_ = new QLabel(QString::fromUtf8("Ш"), topPanel);
        iterationLabel_->setObjectName("iterLabel");
        // Минимальная ширина не дает строке статуса прыгать при изменении текста.
        iterationLabel_->setMinimumWidth(200);

        // serviceGroup — кнопки сброса вида и закрытия окна.
        QWidget* serviceGroup = new QWidget(topPanel);
        serviceGroup->setObjectName("viewerGroup");
        QHBoxLayout* serviceLayout = new QHBoxLayout(serviceGroup);
        serviceLayout->setContentsMargins(0, 0, 0, 0);
        serviceLayout->setSpacing(3);
        resetViewButton_ = new QPushButton(QString::fromUtf8("Сброс вида"), topPanel);
        closeButton_ = new QPushButton(QString::fromUtf8("Закрыть"), topPanel);
        // Кнопка закрытия оформляется как danger-control.
        closeButton_->setProperty("controlRole", "danger");
        serviceLayout->addWidget(resetViewButton_);
        serviceLayout->addWidget(closeButton_);

        controlsTop->addWidget(timelineGroup);
        controlsTop->addWidget(sep1);
        controlsTop->addWidget(zoomGroup);
        controlsTop->addWidget(sep2);
        controlsTop->addWidget(iterationLabel_);
        // stretch выталкивает сервисный блок к правому краю панели.
        controlsTop->addStretch(1);
        controlsTop->addWidget(serviceGroup);
        // Добавляем верхнюю панель и сам график в основную раскладку.
        root->addWidget(topPanel);
        root->addWidget(chartView_, 1);

        // bottomPanel — нижняя панель со слайдерами и статусной строкой.
        QWidget* bottomPanel = new QWidget(this);
        bottomPanel->setObjectName("viewerBottomPanel");
        QVBoxLayout* bottomPanelLayout = new QVBoxLayout(bottomPanel);
        bottomPanelLayout->setContentsMargins(7, 4, 7, 3);
        bottomPanelLayout->setSpacing(2);

        // controlsBottom — строка со слайдером шагов и скоростью.
        QHBoxLayout* controlsBottom = new QHBoxLayout();
        controlsBottom->setSpacing(5);
        QLabel* sliderLabel = new QLabel(QString::fromUtf8("Шаг"), bottomPanel);
        sliderLabel->setObjectName("panelTitle");

        // stepSlider_ управляет текущим шагом показа.
        stepSlider_ = new QSlider(Qt::Horizontal, bottomPanel);
        // Диапазон stepSlider_ совпадает с диапазоном допустимых шагов показа.
        stepSlider_->setRange(0, static_cast<int>(stepMax_));
        // Начальное значение синхронизируем с stepShown_.
        stepSlider_->setValue(static_cast<int>(stepShown_));
        // Минимальная ширина делает длинные трассы удобнее для ручного выбора.
        stepSlider_->setMinimumWidth(320);
        // Насечки скрываем, потому что точное значение видно в верхнем статусе.
        stepSlider_->setTickPosition(QSlider::NoTicks);
        // Интервал насечек оставляем как внутренний ориентир для слайдера.
        stepSlider_->setTickInterval(std::max(1, static_cast<int>(stepMax_ / 20 + 1)));

        // speedSlider_ управляет интервалом таймера автовоспроизведения.
        QLabel* speedTextLabel = new QLabel(QString::fromUtf8("Скорость (мс)"), bottomPanel);
        speedTextLabel->setObjectName("panelTitle");
        speedSlider_ = new QSlider(Qt::Horizontal, bottomPanel);
        // Скорость в миллисекундах ограничиваем разумным диапазоном.
        speedSlider_->setRange(100, 1200);
        // Значение по умолчанию 360 мс дает комфортный autoplay.
        speedSlider_->setValue(360);
        // Фиксированная ширина не дает блоку скорости съесть слишком много места.
        speedSlider_->setFixedWidth(120);
        // speedValueLabel_ сразу показывает текущее числовое значение.
        speedValueLabel_ = new QLabel(QString::number(speedSlider_->value()), bottomPanel);
        speedValueLabel_->setObjectName("speedValueLabel");

        controlsBottom->addWidget(sliderLabel);
        controlsBottom->addWidget(stepSlider_, 1);
        controlsBottom->addSpacing(6);
        controlsBottom->addWidget(speedTextLabel);
        controlsBottom->addWidget(speedSlider_);
        controlsBottom->addWidget(speedValueLabel_);
        bottomPanelLayout->addLayout(controlsBottom);

        // infoLabel_ — нижняя строка с текущей текстовой сводкой по шагу.
        infoLabel_ = new QLabel(bottomPanel);
        infoLabel_->setObjectName("infoLabel");
        // Перенос строк запрещаем, чтобы нижняя строка была одной компактной лентой.
        infoLabel_->setWordWrap(false);
        // Ограничение высоты не дает информационной строке раздувать панель.
        infoLabel_->setMaximumHeight(20);
        bottomPanelLayout->addWidget(infoLabel_);
        root->addWidget(bottomPanel);

        // pathLine_ — сплошная ломаная уже принятой траектории.
        pathLine_ = new QLineSeries();
        pathLine_->setName(QString::fromUtf8("Путь"));
        pathLine_->setColor(QColor(20, 20, 20));
        // Получаем текущий pen серии, чтобы изменить только толщину линии.
        QPen pathPen = pathLine_->pen();
        // Толщина 2.1 делает ломаную читаемой на фоне изолиний.
        pathPen.setWidthF(2.1);
        pathLine_->setPen(pathPen);

        // pathPoints_ — маркеры в узлах принятой траектории.
        pathPoints_ = new QScatterSeries();
        pathPoints_->setName(QString::fromUtf8("Точки"));
        pathPoints_->setColor(QColor(20, 20, 20, 210));
        pathPoints_->setBorderColor(QColor(20, 20, 20, 0));
        pathPoints_->setMarkerSize(5.8);

        // currentPoint_ — увеличенный маркер текущего состояния анимации.
        currentPoint_ = new QScatterSeries();
        currentPoint_->setName(QString::fromUtf8("Текущая точка"));
        currentPoint_->setColor(QColor(245, 130, 32));
        currentPoint_->setBorderColor(QColor(150, 80, 20));
        currentPoint_->setMarkerSize(14.0);

        // Регистрируем все серии в графике.
        chart_->addSeries(pathLine_);
        chart_->addSeries(pathPoints_);
        chart_->addSeries(currentPoint_);

        // Каждую серию привязываем к обеим осям.
        pathLine_->attachAxis(axisX_);
        pathLine_->attachAxis(axisY_);
        pathPoints_->attachAxis(axisX_);
        pathPoints_->attachAxis(axisY_);
        currentPoint_->attachAxis(axisX_);
        currentPoint_->attachAxis(axisY_);

        // Легенду для pathPoints_ скрываем, чтобы она не дублировала pathLine_.
        QList<QLegendMarker*> pointMarkers = chart_->legend()->markers(pathPoints_);
        // pm — индекс по маркерам легенды для series pathPoints_.
        int pm = 0;
        for (pm = 0; pm < pointMarkers.size(); ++pm)
        {
            pointMarkers[pm]->setVisible(false);
        }

        // buttonList нужен только для единообразной настройки курсора.
        std::vector<QPushButton*> buttonList;
        buttonList.push_back(firstButton_);
        buttonList.push_back(prevButton_);
        buttonList.push_back(playPauseButton_);
        buttonList.push_back(nextButton_);
        buttonList.push_back(lastButton_);
        buttonList.push_back(zoomOutButton_);
        buttonList.push_back(zoomInButton_);
        buttonList.push_back(resetViewButton_);
        buttonList.push_back(closeButton_);
        for (std::size_t i = 0; i < buttonList.size(); ++i)
        {
            // Каждую кнопку проверяем на null, чтобы список был безопасен при будущих правках.
            if (buttonList[i] != NULL)
            {
                buttonList[i]->setCursor(Qt::PointingHandCursor);
            }
        }

        // Подсказки помогают быстро понять назначение кнопок без чтения кода.
        firstButton_->setToolTip(QString::fromUtf8("К началу"));
        prevButton_->setToolTip(QString::fromUtf8("Предыдущий шаг"));
        playPauseButton_->setToolTip(QString::fromUtf8("Автовоспроизведение"));
        nextButton_->setToolTip(QString::fromUtf8("Следующий шаг"));
        lastButton_->setToolTip(QString::fromUtf8("К последнему шагу"));
        zoomOutButton_->setToolTip(QString::fromUtf8("Отдалить"));
        zoomInButton_->setToolTip(QString::fromUtf8("Приблизить"));
        resetViewButton_->setToolTip(QString::fromUtf8("Сбросить вид"));
        closeButton_->setToolTip(QString::fromUtf8("Закрыть окно графика"));

        // timer_ двигает анимацию вперед в режиме autoplay.
        timer_ = new QTimer(this);
        // Начальный интервал таймера берем прямо из speedSlider_.
        timer_->setInterval(speedSlider_->value());

        // Связываем все элементы управления с их слотами.
        QObject::connect(firstButton_, &QPushButton::clicked, this, &RosenbrockViewerWidget::OnFirstClicked);
        QObject::connect(prevButton_, &QPushButton::clicked, this, &RosenbrockViewerWidget::OnPrevClicked);
        QObject::connect(zoomOutButton_, &QPushButton::clicked, this, &RosenbrockViewerWidget::OnZoomOutClicked);
        QObject::connect(zoomInButton_, &QPushButton::clicked, this, &RosenbrockViewerWidget::OnZoomInClicked);
        QObject::connect(playPauseButton_,
                         &QPushButton::clicked,
                         this,
                         &RosenbrockViewerWidget::OnPlayPauseClicked);
        QObject::connect(nextButton_, &QPushButton::clicked, this, &RosenbrockViewerWidget::OnNextClicked);
        QObject::connect(lastButton_, &QPushButton::clicked, this, &RosenbrockViewerWidget::OnLastClicked);
        QObject::connect(resetViewButton_,
                         &QPushButton::clicked,
                         this,
                         &RosenbrockViewerWidget::OnResetViewClicked);
        QObject::connect(closeButton_, &QPushButton::clicked, this, &RosenbrockViewerWidget::OnCloseClicked);
        QObject::connect(stepSlider_, &QSlider::valueChanged, this, &RosenbrockViewerWidget::OnStepSliderChanged);
        QObject::connect(speedSlider_,
                         &QSlider::valueChanged,
                         this,
                         &RosenbrockViewerWidget::OnSpeedSliderChanged);
        QObject::connect(timer_, &QTimer::timeout, this, &RosenbrockViewerWidget::OnTimerTick);

        // Отрисовываем стартовое состояние графика.
        UpdatePlot();

        // После полной инициализации виджет считается готовым к показу.
        ready_ = true;
    }

    bool IsReady() const
    {
        // Возвращаем текущее состояние успешности инициализации окна.
        return ready_;
    }

    const std::string& ErrorText() const
    {
        // Возврат по ссылке избегает лишнего копирования строки ошибки.
        return error_;
    }

protected:
    void keyPressEvent(QKeyEvent* event)
    {
        // Даже в protected override подстраховываемся от null event.
        if (event == NULL)
        {
            QWidget::keyPressEvent(event);
            return;
        }

        // key — код нажатой клавиши.
        const int key = event->key();
        // N и стрелка вправо ведут к следующему шагу.
        if (key == Qt::Key_N || key == Qt::Key_Right)
        {
            OnNextClicked();
            return;
        }
        // P и стрелка влево возвращают к предыдущему шагу.
        if (key == Qt::Key_P || key == Qt::Key_Left)
        {
            OnPrevClicked();
            return;
        }
        // Q и Escape закрывают viewer.
        if (key == Qt::Key_Q || key == Qt::Key_Escape)
        {
            OnCloseClicked();
            return;
        }
        // Пробел переключает autoplay.
        if (key == Qt::Key_Space)
        {
            OnPlayPauseClicked();
            return;
        }
        // Home перескакивает к первому шагу.
        if (key == Qt::Key_Home)
        {
            OnFirstClicked();
            return;
        }
        // End перескакивает к последнему шагу.
        if (key == Qt::Key_End)
        {
            OnLastClicked();
            return;
        }

        // Неподдержанные клавиши отдаем стандартному обработчику QWidget.
        QWidget::keyPressEvent(event);
    }

private:
    void SetStepShown(std::size_t value, bool syncSlider)
    {
        // value не должен выходить за допустимый диапазон шагов.
        if (value > stepMax_)
        {
            value = stepMax_;
        }

        // Сохраняем новый шаг как внутреннее состояние виджета.
        stepShown_ = value;

        // При необходимости синхронизируем и видимый slider.
        if (syncSlider && stepSlider_ != NULL)
        {
            // Если слайдер уже синхронен, лишний setValue(...) не нужен.
            if (stepSlider_->value() != static_cast<int>(stepShown_))
            {
                // blockSignals(true) не дает спровоцировать рекурсивный valueChanged.
                const bool old = stepSlider_->blockSignals(true);
                stepSlider_->setValue(static_cast<int>(stepShown_));
                stepSlider_->blockSignals(old);
            }
        }

        // После смены шага полностью перестраиваем картинку.
        UpdatePlot();
    }

    void OnFirstClicked()
    {
        // Ручная навигация всегда останавливает autoplay.
        timer_->stop();
        SetStepShown(0, true);
    }

    void OnPrevClicked()
    {
        timer_->stop();
        // Назад двигаемся только если не стоим уже на первом шаге.
        if (stepShown_ > 0)
        {
            SetStepShown(stepShown_ - 1, true);
        }
    }

    void OnNextClicked()
    {
        timer_->stop();
        // Вперед идем только пока не достигли stepMax_.
        if (stepShown_ < stepMax_)
        {
            SetStepShown(stepShown_ + 1, true);
        }
    }

    void OnLastClicked()
    {
        timer_->stop();
        SetStepShown(stepMax_, true);
    }

    void OnZoomInClicked()
    {
        // Масштабирование возможно только при существующем chartView_.
        if (chartView_ != NULL)
        {
            chartView_->ZoomInSimple();
        }
    }

    void OnZoomOutClicked()
    {
        // То же ограничение действует и для отдаления.
        if (chartView_ != NULL)
        {
            chartView_->ZoomOutSimple();
        }
    }

    void OnResetViewClicked()
    {
        // Сброс вида выполняем только если интерактивный view создан.
        if (chartView_ != NULL)
        {
            chartView_->ResetView();
        }
    }

    void OnPlayPauseClicked()
    {
        // Если autoplay уже идет, эта кнопка работает как пауза.
        if (timer_->isActive())
        {
            timer_->stop();
            UpdatePlot();
            return;
        }

        // При повторном запуске с конца трассы начинаем с шага 0.
        if (stepShown_ >= stepMax_)
        {
            SetStepShown(0, true);
        }

        // Запускаем таймер и сразу обновляем состояние кнопок/статуса.
        timer_->start();
        UpdatePlot();
    }

    void OnStepSliderChanged(int value)
    {
        // Любое ручное движение ползунка должно остановить autoplay.
        timer_->stop();
        // Отрицательный номер шага бессмыслен, поэтому зажимаем его к 0.
        if (value < 0)
        {
            value = 0;
        }
        SetStepShown(static_cast<std::size_t>(value), false);
    }

    void OnSpeedSliderChanged(int value)
    {
        // Защищаемся от слишком маленьких интервалов, которые перегружают GUI.
        if (value < 50)
        {
            value = 50;
        }

        // Новый интервал сразу передаем таймеру.
        timer_->setInterval(value);
        // И синхронно обновляем текстовую подпись рядом со слайдером.
        speedValueLabel_->setText(QString::number(value));
    }

    void OnTimerTick()
    {
        // На последнем шаге таймер сам останавливает проигрывание.
        if (stepShown_ >= stepMax_)
        {
            timer_->stop();
            UpdatePlot();
            return;
        }

        // Иначе просто двигаем просмотр на один шаг вперед.
        SetStepShown(stepShown_ + 1, true);
    }

    void OnCloseClicked()
    {
        // Перед закрытием окна обязательно тушим активный timer_.
        timer_->stop();
        // Затем вызываем стандартное закрытие QWidget.
        close();
    }

    void UpdatePlot()
    {
        // state — полностью подготовленное состояние для текущего шага показа.
        StepVisualState state;
        BuildStateUntilStep(result_, stepShown_, state);

        // Перед новой отрисовкой очищаем серии от старых данных.
        pathLine_->clear();
        pathPoints_->clear();
        currentPoint_->clear();

        std::size_t i = 0;
        for (i = 0; i < state.acceptedPath.size(); ++i)
        {
            // В 2D нужны как минимум две координаты, иначе точку рисовать нельзя.
            if (state.acceptedPath[i].size() < 2)
            {
                continue;
            }
            // x — первая координата принятой точки пути.
            const double x = state.acceptedPath[i][0];
            // y — вторая координата той же точки.
            const double y = state.acceptedPath[i][1];
            // Одни и те же координаты подаются и в ломаную, и в маркеры точек.
            pathLine_->append(x, y);
            pathPoints_->append(x, y);
        }

        // shownCurrent — фактическая точка, которую надо подсветить сейчас.
        Point2D shownCurrent = state.currentPoint;
        bool hasShownCurrent = state.hasCurrentPoint;
        // Если текущий шаг соответствует реальной trace-строке, уточняем current marker по этой строке.
        if (state.hasRow && stepShown_ > 0 && stepShown_ <= result_.trace.size())
        {
            const OptimizationResult::TraceRow& row = result_.trace[stepShown_ - 1];
            // При успешном шаге текущая видимая точка — это принятый trialPoint.
            if (row.successfulStep && row.trialPoint.size() >= 2)
            {
                shownCurrent.x = row.trialPoint[0];
                shownCurrent.y = row.trialPoint[1];
                hasShownCurrent = true;
            }
            // При неудачном шаге остаемся в исходной точке yi.
            else if (row.yi.size() >= 2)
            {
                shownCurrent.x = row.yi[0];
                shownCurrent.y = row.yi[1];
                hasShownCurrent = true;
            }
        }

        // currentPoint_ обновляем только если координаты текущей точки действительно известны.
        if (hasShownCurrent)
        {
            currentPoint_->append(shownCurrent.x, shownCurrent.y);
        }

        // Все оверлейные сегменты и inset-базис рисуются через chartView_.
        if (chartView_ != NULL)
        {
            chartView_->SetFailedSegments(state.failedS1Segments, state.failedS2Segments);

            // s1Segments/s2Segments содержат только удачные отрезки по своим
            // направлениям на уже показанном префиксе.
            std::vector<LineSegment> s1Segments;
            std::vector<LineSegment> s2Segments;
            for (i = 0; i < stepShown_ && i < result_.trace.size(); ++i)
            {
                const OptimizationResult::TraceRow& row = result_.trace[i];
                // Удачный сегмент можно собрать только если известны обе его точки.
                if (!row.successfulStep || row.yi.size() < 2 || row.trialPoint.size() < 2)
                {
                    continue;
                }

                // seg — один принятый отрезок траектории по одному направлению.
                LineSegment seg;
                seg.from.x = row.yi[0];
                seg.from.y = row.yi[1];
                seg.to.x = row.trialPoint[0];
                seg.to.y = row.trialPoint[1];

                // j == 1 означает шаг вдоль первого текущего направления.
                if (row.j == 1)
                {
                    s1Segments.push_back(seg);
                }
                // j == 2 означает шаг вдоль второго направления.
                else if (row.j == 2)
                {
                    s2Segments.push_back(seg);
                }
            }
            chartView_->SetDirectionalSegments(s1Segments, s2Segments);

            // Активный отрезок — это именно текущая строка, если она существует.
            if (state.hasRow && stepShown_ > 0 && stepShown_ <= result_.trace.size())
            {
                const OptimizationResult::TraceRow& row = result_.trace[stepShown_ - 1];
                // Активный сегмент можно показать только если известны и start, и end точки.
                if (row.yi.size() >= 2 && row.trialPoint.size() >= 2)
                {
                    LineSegment active;
                    active.from.x = row.yi[0];
                    active.from.y = row.yi[1];
                    active.to.x = row.trialPoint[0];
                    active.to.y = row.trialPoint[1];

                    // color выбираем по типу шага и номеру направления.
                    QColor color = QColor(120, 120, 120, 210);
                    // Неудачный шаг всегда рисуем красным.
                    if (!row.successfulStep)
                    {
                        color = QColor(210, 55, 55, 210);
                    }
                    // Удачный шаг по s1 делаем синим.
                    else if (row.j == 1)
                    {
                        color = QColor(36, 121, 255, 225);
                    }
                    // Удачный шаг по s2 делаем зеленым.
                    else if (row.j == 2)
                    {
                        color = QColor(10, 170, 120, 225);
                    }
                    // Успешные шаги сплошные, неудачные — пунктирные.
                    const Qt::PenStyle style = row.successfulStep ? Qt::SolidLine : Qt::DashLine;
                    chartView_->SetActiveSegment(active, color, style);
                }
                else
                {
                    chartView_->ClearActiveSegment();
                }
            }
            else
            {
                chartView_->ClearActiveSegment();
            }

            if (state.hasNewBasis)
            {
                // По умолчанию basisOrigin берем из уже собранного visual state.
                Point2D basisOrigin = state.newBasisOrigin;
                // Если есть текущая строка, стараемся привязать inset к ее yi.
                if (state.hasRow && stepShown_ > 0 && stepShown_ <= result_.trace.size())
                {
                    const OptimizationResult::TraceRow& row = result_.trace[stepShown_ - 1];
                    // Для смещения inset-дирекций достаточно опорной точки yi.
                    if (row.yi.size() >= 2)
                    {
                        basisOrigin.x = row.yi[0];
                        basisOrigin.y = row.yi[1];
                    }
                }
                chartView_->SetNewBasis(basisOrigin, state.newBasisDirections);
            }
            else
            {
                chartView_->ClearNewBasis();
            }
        }

        // title — заголовок графика с текущими k и j.
        QString title = QString::fromUtf8("Метод Розенброка (2D)");
        // Если текущий shown step совпадает с реальной строкой трассировки, показываем ее k и j.
        if (state.hasRow)
        {
            title += QString::fromUtf8("  |  k=") +
                     QString::number(static_cast<unsigned long long>(state.k)) +
                     QString::fromUtf8(", j=") +
                     QString::number(static_cast<unsigned long long>(state.j));
        }
        chart_->setTitle(title);

        // iterText — краткий статус в верхней панели.
        QString iterText = QString::fromUtf8("Ш ") +
                           QString::number(static_cast<unsigned long long>(stepShown_)) +
                           QString::fromUtf8(" / ") +
                           QString::number(static_cast<unsigned long long>(stepMax_)) +
                           QString::fromUtf8("   k ") +
                           QString::number(static_cast<unsigned long long>(state.k)) +
                           QString::fromUtf8(" / ") +
                           QString::number(static_cast<unsigned long long>(kMax_));
        iterationLabel_->setText(iterText);

        // info — нижняя текстовая строка со статистикой и текущей точкой.
        std::ostringstream info;
        info << "Удачных шагов: " << state.successfulCount << "   |   Неудачных: " << state.failedCount;

        // Блок с x и f(x) имеет смысл только если текущая точка определена.
        if (hasShownCurrent)
        {
            // fx — значение функции в текущей подсвеченной точке.
            double fx = 0.0;
            // evalError — локальный буфер ошибки Evaluate(...), не выводимый наружу.
            std::string evalError;
            // p — временный вектор формата IObjectiveFunction.
            Vector p(2, 0.0);
            p[0] = shownCurrent.x;
            p[1] = shownCurrent.y;

            // Значение функции добавляем в строку только при успешном вычислении.
            if (function_ != NULL && function_->Evaluate(p, fx, evalError))
            {
                info << "   |   x=" << FormatPoint(shownCurrent);
                info << "   |   f(x)=" << std::fixed << std::setprecision(6) << fx;
            }
        }

        // Данные по текущему шагу есть только если есть реальная row из трассировки.
        if (state.hasRow)
        {
            info << "   |   Δ=" << std::fixed << std::setprecision(6) << state.delta;
            info << "   |   шаг: " << (state.successfulStep ? "успешный" : "неудачный");
            // rollback отображаем только если он реально зафиксирован на этом проходе.
            if (state.rollback)
            {
                info << "   |   откат: да";
            }
            // directionChanged отображаем только при реальной перестройке направлений.
            if (state.directionChanged)
            {
                info << "   |   смена направления: да";
            }
        }

        // Переносим собранную std::string в QLabel.
        infoLabel_->setText(QString::fromStdString(info.str()));

        // Управляющие кнопки выключаем на краях диапазона шагов.
        firstButton_->setEnabled(stepShown_ > 0);
        prevButton_->setEnabled(stepShown_ > 0);
        nextButton_->setEnabled(stepShown_ < stepMax_);
        lastButton_->setEnabled(stepShown_ < stepMax_);
        playPauseButton_->setText(timer_->isActive() ? QString::fromUtf8("❚❚")
                                                     : QString::fromUtf8("▶"));

        // Если задан внешний callback, синхронно уведомляем его о новом shown step.
        if (onStepChanged_)
        {
            onStepChanged_(stepShown_, stepMax_, state.k);
        }
    }

private:
    // function_ — функция, соответствующая текущему запуску визуализируемого метода.
    const IObjectiveFunction* function_;
    // result_ — трассировка и итог метода Розенброка.
    const OptimizationResult& result_;
    // bounds_ — исходные координатные границы графика.
    PlotBounds2D bounds_;

    // kMax_ — максимальный номер внешней итерации среди trace-строк.
    std::size_t kMax_;
    // stepMax_ — последний допустимый шаг показа.
    std::size_t stepMax_;
    // stepShown_ — шаг, который сейчас отображается в viewer.
    std::size_t stepShown_;

    // ready_ — флаг успешной инициализации виджета.
    bool ready_;
    // error_ — текст ошибки, если инициализация viewer завершилась неуспешно.
    std::string error_;

    // chart_ — основной объект Qt Charts.
    QChart* chart_;
    // chartView_ — пользовательская интерактивная обертка над chart_.
    InteractiveChartView* chartView_;
    // axisX_ — ось первой координаты.
    QValueAxis* axisX_;
    // axisY_ — ось второй координаты.
    QValueAxis* axisY_;

    // pathLine_ — сплошная линия принятой траектории.
    QLineSeries* pathLine_;
    // pathPoints_ — маркеры узлов той же траектории.
    QScatterSeries* pathPoints_;
    // currentPoint_ — выделенный маркер текущего шага.
    QScatterSeries* currentPoint_;

    // timer_ — источник тиков autoplay.
    QTimer* timer_;

    // firstButton_ — переход к началу.
    QPushButton* firstButton_;
    // prevButton_ — переход на один шаг назад.
    QPushButton* prevButton_;
    // zoomOutButton_ — уменьшение масштаба.
    QPushButton* zoomOutButton_;
    // zoomInButton_ — увеличение масштаба.
    QPushButton* zoomInButton_;
    // playPauseButton_ — запуск/пауза autoplay.
    QPushButton* playPauseButton_;
    // nextButton_ — переход на один шаг вперед.
    QPushButton* nextButton_;
    // lastButton_ — переход к последнему шагу.
    QPushButton* lastButton_;
    // resetViewButton_ — сброс zoom/pan к исходным bounds.
    QPushButton* resetViewButton_;
    // closeButton_ — закрытие окна визуализации.
    QPushButton* closeButton_;

    // stepSlider_ — ручной выбор шага показа.
    QSlider* stepSlider_;
    // speedSlider_ — управление интервалом таймера.
    QSlider* speedSlider_;
    // speedValueLabel_ — численная подпись текущего интервала.
    QLabel* speedValueLabel_;

    // iterationLabel_ — верхний короткий статус шага и итерации.
    QLabel* iterationLabel_;
    // infoLabel_ — нижняя текстовая строка со статистикой и значением f(x).
    QLabel* infoLabel_;
    // onStepChanged_ — callback синхронизации со строкой таблицы.
    ViewerStepChangedCallback onStepChanged_;
};

// ----------------------------------------------------------------------------
// ФУНКЦИЯ SHOWROSENBROCK2DVIEWER
// ----------------------------------------------------------------------------
// Назначение:
// Создать и показать отдельное 2D-окно визуализации метода Розенброка.
// ----------------------------------------------------------------------------
bool ShowRosenbrock2DViewer(const IObjectiveFunction* function,
                            const OptimizationResult& result,
                            std::string& error,
                            std::size_t initialStep,
                            const ViewerStepChangedCallback& onStepChanged)
{
    // На старте очищаем внешний текст ошибки.
    error.clear();

    // Без функции визуализатор не сможет ни построить изолинии, ни считать f(x).
    if (function == NULL)
    {
        error = "Функция не задана";
        return false;
    }
    // Этот viewer рассчитан только на двумерную постановку.
    if (function->Dimension() != 2)
    {
        error = "Визуализация Qt Charts поддерживает только 2D";
        return false;
    }
    // Пустая трассировка означает, что рисовать просто нечего.
    if (result.trace.empty())
    {
        error = "Трассировка пуста: нечего визуализировать";
        return false;
    }

    // bounds — стартовые границы обзора графика.
    PlotBounds2D bounds = BuildBounds(result);

    // kMax нужен для верхней статусной панели вида "k a / b".
    const std::size_t kMax = MaxIterationK(result);

    // Перед созданием viewer готовим plugin-path для корректной загрузки Qt platform plugin.
    if (!PrepareQtRuntimePlugins(error))
    {
        return false;
    }

    // Локальные argc/argv нужны только если QApplication еще не существует.
    int argc = 0;
    // argv оставляем пустым: локальный QApplication поднимается без CLI-аргументов.
    char** argv = NULL;

    // Если приложение уже работает в GUI-режиме, берем существующий instance.
    QApplication* app = qobject_cast<QApplication*>(QCoreApplication::instance());
    // ownsApp показывает, нужно ли нам потом вручную удалять QApplication.
    bool ownsApp = false;

    // В standalone-режиме viewer сам поднимает собственный QApplication.
    if (app == NULL)
    {
        app = new QApplication(argc, argv);
        ownsApp = true;
    }

    // Явно добавляем plugin-путь, если он уже был подготовлен выше.
    const QString pluginsPath = QString::fromLocal8Bit(qgetenv("QT_PLUGIN_PATH"));
    // Дополнительный library path добавляем только если он реально известен.
    if (!pluginsPath.isEmpty())
    {
        app->addLibraryPath(pluginsPath);
    }

    // view — отдельное окно 2D-визуализации.
    RosenbrockViewerWidget* view =
        new RosenbrockViewerWidget(function, result, bounds, kMax, initialStep, onStepChanged, NULL);
    // Если viewer не дошел до ready state, забираем текст ошибки и чистим временные объекты.
    if (!view->IsReady())
    {
        error = view->ErrorText();
        delete view;

        // Удаляем QApplication только если создали его внутри этой функции.
        if (ownsApp)
        {
            delete app;
        }
        return false;
    }

    // Окно делаем немодальным, чтобы главная форма оставалась доступна.
    view->setWindowModality(Qt::NonModal);
    view->setAttribute(Qt::WA_DeleteOnClose, true);
    view->show();
    view->activateWindow();
    view->raise();

    // В standalone-режиме нужно вручную запустить event loop Qt.
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

bool ShowRosenbrock2DViewer(const IObjectiveFunction*,
                            const OptimizationResult&,
                            std::string& error,
                            std::size_t,
                            const ViewerStepChangedCallback&)
{
    error = "Qt Charts не подключен в текущей сборке";
    return false;
}

}  // namespace lab2

#endif
