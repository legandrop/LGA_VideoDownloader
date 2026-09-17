#ifndef LOGVIEW_H
#define LOGVIEW_H

#include <QAbstractScrollArea>
#include <QDateTime>
#include <QFrame>
#include <QVector>

#include "loglevel.h"

class QLabel;
class QPushButton;
class FilterButton;

// Cuerpo del log: pinta solo las filas visibles (hora, nivel, mensaje con elipsis). Agregar
// una linea no reestiliza nada: guarda la entrada, ajusta el scroll y repinta.
class LogCanvas : public QAbstractScrollArea
{
    Q_OBJECT
public:
    struct Entry {
        QTime time;
        LogLevel level;
        QString text;
    };
    enum class Filter { All, Warnings, Errors };

    explicit LogCanvas(QWidget *parent = nullptr);

    void append(const QString &text, LogLevel level, const QTime &time);
    void clear();
    void setFilter(Filter filter);
    // Texto plano de lo que muestra el filtro actual (para copiar).
    QString plainText() const;
    int count(Filter filter) const;

signals:
    void countsChanged();

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    bool viewportEvent(QEvent *event) override;

private:
    bool matches(const Entry &entry) const;
    void rebuildVisible();
    void updateScrollRange();
    int rowHeight() const;

    QVector<Entry> m_entries;
    QVector<int> m_visible;  // indices de m_entries que pasan el filtro
    int m_first = 0;         // indice logico de m_entries[0] (sube al descartar lo viejo)
    Filter m_filter = Filter::All;
    int m_warnings = 0;
    int m_errors = 0;
};

// Tarjeta "Log": encabezado con filtros y acciones, y el LogCanvas siempre visible.
class LogView : public QFrame
{
    Q_OBJECT
public:
    explicit LogView(QWidget *parent = nullptr);
    void append(const QString &text, LogLevel level, const QTime &time = QTime::currentTime());
    LogCanvas *canvas() const { return m_canvas; }

private:
    void refreshCounts();

    LogCanvas *m_canvas;
    FilterButton *m_all;
    FilterButton *m_warnings;
    FilterButton *m_errors;
    QPushButton *m_copy;
    QPushButton *m_clear;
};

#endif // LOGVIEW_H
