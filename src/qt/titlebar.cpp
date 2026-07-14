// Copyright (c) 2026 The Custom BCHN authors
// Distributed under the MIT software license.

#include <qt/titlebar.h>

#include <QHBoxLayout>
#include <QLabel>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPixmap>
#include <QPushButton>

CustomTitleBar::CustomTitleBar(QWidget *window, QMenuBar *menuBar,
                               const QIcon &icon, const QString &title,
                               QWidget *parent)
    : QWidget(parent), m_window(window) {
    setObjectName(QStringLiteral("customTitleBar"));
    setFixedHeight(40);
    setAttribute(Qt::WA_StyledBackground, true);

    m_iconLabel = new QLabel(this);
    m_iconLabel->setObjectName(QStringLiteral("titleBarIcon"));
    m_iconLabel->setPixmap(icon.pixmap(20, 20));
    m_iconLabel->setFixedSize(24, 24);
    m_iconLabel->setAlignment(Qt::AlignCenter);

    m_titleLabel = new QLabel(title, this);
    m_titleLabel->setObjectName(QStringLiteral("titleBarTitle"));

    m_minButton = new QPushButton(QStringLiteral("\u2013"), this);
    m_minButton->setObjectName(QStringLiteral("titleBarMinButton"));
    m_minButton->setFixedSize(40, 30);
    m_minButton->setFocusPolicy(Qt::NoFocus);
    connect(m_minButton, &QPushButton::clicked, m_window, &QWidget::showMinimized);

    m_maxButton = new QPushButton(QStringLiteral("\u25A2"), this);
    m_maxButton->setObjectName(QStringLiteral("titleBarMaxButton"));
    m_maxButton->setFixedSize(40, 30);
    m_maxButton->setFocusPolicy(Qt::NoFocus);
    connect(m_maxButton, &QPushButton::clicked, this,
            &CustomTitleBar::toggleMaximize);

    m_closeButton = new QPushButton(QStringLiteral("\u2715"), this);
    m_closeButton->setObjectName(QStringLiteral("titleBarCloseButton"));
    m_closeButton->setFixedSize(40, 30);
    m_closeButton->setFocusPolicy(Qt::NoFocus);
    connect(m_closeButton, &QPushButton::clicked, m_window, &QWidget::close);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 4, 4, 4);
    layout->setSpacing(10);
    layout->addWidget(m_iconLabel);
    layout->addWidget(m_titleLabel);
    if (menuBar) {
        menuBar->setObjectName(QStringLiteral("titleBarMenu"));
        layout->addSpacing(12);
        layout->addWidget(menuBar);
    }
    layout->addStretch(1);
    layout->addWidget(m_minButton);
    layout->addWidget(m_maxButton);
    layout->addWidget(m_closeButton);
}

void CustomTitleBar::setTitle(const QString &title) {
    m_titleLabel->setText(title);
}

void CustomTitleBar::setIcon(const QIcon &icon) {
    m_iconLabel->setPixmap(icon.pixmap(20, 20));
}

void CustomTitleBar::toggleMaximize() {
    if (m_window->isMaximized()) {
        m_window->showNormal();
        m_maxButton->setText(QStringLiteral("\u25A2"));
    } else {
        m_window->showMaximized();
        m_maxButton->setText(QStringLiteral("\u2750"));
    }
}

void CustomTitleBar::mousePressEvent(QMouseEvent *e) {
    if (e->button() == Qt::LeftButton && !m_window->isMaximized()) {
        m_dragging = true;
        m_dragOffset = e->globalPos() - m_window->frameGeometry().topLeft();
        e->accept();
    }
}

void CustomTitleBar::mouseMoveEvent(QMouseEvent *e) {
    if (m_dragging && (e->buttons() & Qt::LeftButton)) {
        m_window->move(e->globalPos() - m_dragOffset);
        e->accept();
    }
}

void CustomTitleBar::mouseReleaseEvent(QMouseEvent *e) {
    m_dragging = false;
    QWidget::mouseReleaseEvent(e);
}

void CustomTitleBar::mouseDoubleClickEvent(QMouseEvent *e) {
    if (e->button() == Qt::LeftButton) {
        toggleMaximize();
    }
}
