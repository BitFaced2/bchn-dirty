// Copyright (c) 2026 The Custom BCHN authors
// Distributed under the MIT software license.

#pragma once

#include <QIcon>
#include <QString>
#include <QWidget>

class QLabel;
class QMenuBar;
class QMouseEvent;
class QPushButton;

class CustomTitleBar : public QWidget {
    Q_OBJECT

public:
    CustomTitleBar(QWidget *window, QMenuBar *menuBar, const QIcon &icon,
                   const QString &title, QWidget *parent = nullptr);

    void setTitle(const QString &title);
    void setIcon(const QIcon &icon);

protected:
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;

private Q_SLOTS:
    void toggleMaximize();

private:
    QWidget *m_window;
    QLabel *m_iconLabel;
    QLabel *m_titleLabel;
    QPushButton *m_minButton;
    QPushButton *m_maxButton;
    QPushButton *m_closeButton;
    QPoint m_dragOffset;
    bool m_dragging = false;
};
