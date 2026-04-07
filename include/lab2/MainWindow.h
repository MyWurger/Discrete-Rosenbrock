// ============================================================================
// ФАЙЛ MAINWINDOW.H - ОБЪЯВЛЕНИЕ ГЛАВНОГО ОКНА ПРИЛОЖЕНИЯ LAB_2
// ============================================================================
// Назначение файла:
// 1) Объявить класс MainWindow, который управляет всем GUI приложения.
// 2) Описать связи между полями ввода, парсером функции, методом Розенброка,
//    таблицей трассировки и графическими окнами.
// 3) Хранить текущее состояние расчета, чтобы синхронизировать сводку,
//    стационарную точку, таблицу и визуализацию.
// ============================================================================

// Защита от повторного включения заголовочного файла.
#pragma once

// Подключаем парсер пользовательской функции и связанные интерфейсы.
#include "ObjectiveParser.h"
// Подключаем типы метода Розенброка: запрос, результат, векторы и интерфейс оптимизатора.
#include "RosenbrockDiscreteMethod.h"

// Подключаем базовый класс главного окна Qt.
#include <QMainWindow>

// std::size_t нужен для размерностей, индексов шагов и счетчиков.
#include <cstddef>
// std::vector нужен для хранения ширин столбцов и других списков.
#include <vector>

// Предварительное объявление QLabel для текстовых подписей и итоговых значений.
class QLabel;
// Предварительное объявление QLineEdit для полей ввода параметров задачи.
class QLineEdit;
// Предварительное объявление QPushButton для кнопок интерфейса.
class QPushButton;
// Предварительное объявление QTableWidget для таблицы трассировки метода.
class QTableWidget;

namespace lab2
{

// ----------------------------------------------------------------------------
// КЛАСС MainWindow - ГЛАВНОЕ ОКНО GUI ДЛЯ МЕТОДА РОЗЕНБРОКА
// ----------------------------------------------------------------------------
// Класс отвечает за:
// - ввод функции и параметров метода;
// - запуск оптимизации;
// - показ итогов и стационарной точки;
// - заполнение таблицы трассировки;
// - открытие отдельного окна графика и экспорт таблицы в PNG.
// ----------------------------------------------------------------------------
class MainWindow : public QMainWindow
{
public:
    // Конструктор главного окна.
    // Принимает:
    // - optimizer: объект метода Розенброка с дискретным шагом;
    // - parser: парсер текстовой функции в вычислимый объект;
    // - parent: родительский виджет Qt (обычно NULL для главного окна).
    MainWindow(const IOptimizer& optimizer, const IObjectiveParser& parser, QWidget* parent = NULL);

    // Деструктор главного окна.
    // Освобождает принадлежащие окну ресурсы и удаляет текущую пользовательскую функцию.
    ~MainWindow();

private:
    // Перехватчик событий Qt.
    // Используется для тонкой обработки взаимодействия с полями ввода и другими виджетами.
    bool eventFilter(QObject* watched, QEvent* event) override;

    // Создает и размещает все элементы главного окна.
    void BuildUi();

    // Применяет тему, стили и визуальные настройки интерфейса.
    void ApplyTheme();

    // Подключает сигналы и слоты для кнопок, полей ввода и таблицы.
    void SetupSignals();

    // Загружает стартовые значения функции и параметров по умолчанию.
    void LoadDefaultValues();

    // Считывает текущие входные данные, запускает метод и обновляет интерфейс.
    void Solve();

    // Открывает отдельное окно с графической визуализацией результата.
    void OpenGraphWindow();

    // Экспортирует текущую таблицу трассировки целиком в PNG-файл.
    void ExportTraceTablePng();

    // Очищает текущее состояние результата перед новым запуском метода.
    void ClearResultState();

    // Удаляет текущий объект пользовательской функции, если он уже был создан.
    void DeleteCurrentFunction();

    // Заполняет таблицу трассировки строками из текущего результата.
    void FillTraceTable();

    // Обновляет панель итогов оптимизации.
    void UpdateSummaryPanel();

    // Обновляет панель стационарной точки, рассчитанной через производную.
    void UpdateStationaryPanel();

    // Подсвечивает в таблице строку, соответствующую текущему шагу визуализации.
    void HighlightTableRowByStep(std::size_t shownStep, std::size_t totalSteps, std::size_t k);

    // Обновляет подсказку о автоматически определенной размерности функции.
    void UpdateDimensionHint();

    // Проверяет корректность выражения функции.
    // При ошибке возвращает false и записывает пояснение в errorText.
    bool ValidateFunctionExpression(QString& errorText) const;

    // Онлайн-обработчик изменения текста функции.
    void HandleFunctionTextChanged(const QString& text);

    // Онлайн-обработчик изменения входных параметров.
    void HandleInputsChanged();

    // Устанавливает визуальное состояние валидности для конкретного поля ввода.
    void ApplyFieldValidation(QLineEdit* edit, bool valid, const QString& toolTip) const;

    // Показывает статус вычисления: успех, ошибка или информационное сообщение.
    void SetStatus(const QString& text, bool success);

    // Собирает OptimizationRequest из текущего состояния GUI.
    // Дополнительно возвращает:
    // - expression: нормализованный текст выражения;
    // - detectedDimension: определенную размерность;
    // - errorText: текст ошибки, если входные данные некорректны.
    bool BuildRequestFromInput(OptimizationRequest& request,
                               std::string& expression,
                               std::size_t& detectedDimension,
                               QString& errorText);

    // Разбирает текст вектора стартовой точки в набор координат.
    bool ParseVectorText(const QString& text, std::size_t expectedSize, Vector& values) const;

    // Разбирает вещественное число из строки с поддержкой допустимых форматов ввода.
    static bool ParseNumberText(const QString& text, double& value);

    // Определяет максимальный индекс переменной x_i, встреченной в выражении.
    static std::size_t DetectDimensionFromExpression(const std::string& expression);

private:
    // Ссылка на объект оптимизатора, который реально выполняет метод Розенброка.
    const IOptimizer& optimizer_;
    // Ссылка на парсер, который компилирует текст функции в вычислимый объект.
    const IObjectiveParser& parser_;

    // Текущая пользовательская функция, построенная из текстового выражения.
    IObjectiveFunction* currentFunction_;
    // Текущий запрос к оптимизатору, собранный из полей ввода.
    OptimizationRequest currentRequest_;
    // Текущий результат оптимизации.
    OptimizationResult currentResult_;
    // Признак того, что в окне уже есть валидный результат расчета.
    bool hasResult_;

    // Заголовок окна/основной заголовок интерфейса.
    QLabel* titleLabel_;
    // Статусная строка с сообщением об успехе или ошибке.
    QLabel* statusLabel_;
    // Подсказка с автоматически определенной размерностью функции.
    QLabel* dimensionHintLabel_;

    // Поле ввода выражения функции.
    QLineEdit* expressionEdit_;
    // Поле ввода стартовой точки x0.
    QLineEdit* startPointEdit_;
    // Поле ввода точности epsilon.
    QLineEdit* epsilonEdit_;
    // Поле ввода коэффициента растяжения alpha.
    QLineEdit* alphaEdit_;
    // Поле ввода коэффициента сжатия beta.
    QLineEdit* betaEdit_;
    // Поле ввода базовой длины шага Delta_0.
    QLineEdit* stepEdit_;

    // Кнопка запуска расчета.
    QPushButton* solveButton_;
    // Кнопка открытия отдельного графического окна.
    QPushButton* openGraphButton_;
    // Кнопка экспорта таблицы трассировки в PNG.
    QPushButton* exportTraceButton_;

    // Поле вывода найденной оптимальной точки x*.
    QLabel* optimumPointValueLabel_;
    // Поле вывода оптимального значения функции f(x*).
    QLabel* optimumFunctionValueLabel_;
    // Поле вывода числа внешних итераций k.
    QLabel* iterationsValueLabel_;
    // Поле вывода числа вычислений функции.
    QLabel* evaluationsValueLabel_;
    // Поле вывода разницы |f(x_ст) - f(x*)|.
    QLabel* summaryDeltaValueLabel_;
    // Поле вывода стационарной точки, найденной через производную.
    QLabel* stationaryPointValueLabel_;
    // Поле вывода значения функции в стационарной точке.
    QLabel* stationaryFunctionValueLabel_;
    // Поле вывода расстояния между x_ст и x*.
    QLabel* stationaryDistanceValueLabel_;
    // Поле вывода разности значений функции в x_ст и x*.
    QLabel* stationaryDeltaValueLabel_;

    // Таблица трассировки шагов метода.
    QTableWidget* traceTable_;
    // Сохраненные пользовательские ширины столбцов таблицы между перезапусками расчета.
    std::vector<int> savedTraceColumnWidths_;

    // Признак того, что стационарная точка для текущей функции успешно вычислена.
    bool stationaryAvailable_;
    // Координаты стационарной точки, найденной через систему производных.
    Vector stationaryPoint_;
    // Значение функции в стационарной точке.
    double stationaryValue_;
    // Расстояние между стационарной точкой и найденной методом точкой x*.
    double stationaryDistance_;
    // Разность |f(x_ст) - f(x*)|.
    double stationaryDelta_;
    // Дополнительное текстовое сообщение по стационарной точке.
    std::string stationaryMessage_;
};

}  // namespace lab2
