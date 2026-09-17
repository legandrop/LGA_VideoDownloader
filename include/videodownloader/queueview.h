#ifndef QUEUEVIEW_H
#define QUEUEVIEW_H

#include <QFrame>
#include <QHash>

#include "downloaditem.h"

class Chip;
class ElidedLabel;
class ProgressLine;
class QLabel;
class QLayout;
class QPushButton;
class QScrollArea;
class QStackedWidget;
class QVBoxLayout;
class StatusBadge;

// Tarjeta de un link en la cola: estado, titulo, progreso y, si fallo, la explicacion con
// la solucion y los botones para reintentar.
class QueueCard : public QFrame
{
    Q_OBJECT
public:
    explicit QueueCard(int id, QWidget *parent = nullptr);
    // position: lugar entre los pendientes (1 = el proximo); firefoxAvailable: ofrecer
    // "Retry with Firefox" en los errores de sesion.
    void setItem(const DownloadItem &item, int position, bool firefoxAvailable);

signals:
    void cancelRequested(int id);
    void removeRequested(int id);
    void retryRequested(int id);
    void retryWithBrowserRequested(int id, const QString &browserKey);
    void showRequested(int id);
    void copyErrorRequested(int id);

private:
    void setState(const QString &state);

    int m_id;
    QString m_state;
    StatusBadge *m_badge;
    ElidedLabel *m_title;
    Chip *m_source;
    Chip *m_stateChip;
    QWidget *m_progressRow;
    ProgressLine *m_progress;
    QLabel *m_percent;
    QLabel *m_meta;
    QLabel *m_headline;
    QFrame *m_fixBox;
    QLabel *m_fixText;
    QPushButton *m_retryBrowser;
    QPushButton *m_retry;
    QPushButton *m_copyError;
    QPushButton *m_show;
    QPushButton *m_cancel;
    QPushButton *m_remove;
    QLayout *m_actions;
};

// Tarjeta "Queue": encabezado con el resumen, lista de QueueCard (o el estado vacio) y pie
// con el progreso general y las acciones sobre toda la cola.
class QueueView : public QFrame
{
    Q_OBJECT
public:
    explicit QueueView(QWidget *parent = nullptr);

    void upsertItem(const DownloadItem &item);
    void removeItem(int id);
    void setFirefoxAvailable(bool available);
    void setWaitingForTools(bool waiting);

signals:
    void cancelRequested(int id);
    void removeRequested(int id);
    void retryRequested(int id);
    void retryWithBrowserRequested(int id, const QString &browserKey);
    void showRequested(int id);
    void copyErrorRequested(int id);
    void clearFinishedRequested();
    void cancelAllRequested();
    void retryFailedRequested();

private:
    void refresh();
    void refreshSummary();

    QHash<int, DownloadItem> m_items;
    QList<int> m_order;
    QHash<int, QueueCard *> m_cards;
    bool m_firefoxAvailable = false;
    bool m_waitingForTools = false;

    QLabel *m_count;
    QStackedWidget *m_stack;
    QWidget *m_listHost;
    QVBoxLayout *m_listLayout;
    QScrollArea *m_scroll;
    QFrame *m_footer;
    QLabel *m_overallLabel;
    ProgressLine *m_overall;
    QLabel *m_overallValue;
    QLabel *m_attention;
    QPushButton *m_clearFinished;
    QPushButton *m_retryFailed;
    QPushButton *m_cancelAll;
};

#endif // QUEUEVIEW_H
