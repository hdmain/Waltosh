#include "MnemonicBackupDialog.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRandomGenerator>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace {

void wipeQString(QString& s)
{
    for (QChar& ch : s) {
        ch = QChar(0);
    }
    s.clear();
}

} // namespace

MnemonicBackupDialog::MnemonicBackupDialog(const QString& mnemonic, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Backup your mnemonic"));
    setModal(true);
    setMinimumWidth(480);

    const QStringList raw = mnemonic.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    m_words.reserve(raw.size());
    for (const QString& w : raw) {
        m_words.push_back(w.trimmed().toLower());
    }

    auto* root = new QVBoxLayout(this);
    auto* stack = new QStackedWidget(this);

    m_showPage = new QWidget(stack);
    auto* showLayout = new QVBoxLayout(m_showPage);
    auto* intro = new QLabel(
        QStringLiteral(
            "Write these words down offline. Waltosh shows them only once and does not store them."),
        m_showPage);
    intro->setWordWrap(true);
    showLayout->addWidget(intro);

    auto* grid = new QGridLayout;
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(8);
    for (int i = 0; i < m_words.size(); ++i) {
        auto* idx = new QLabel(QString::number(i + 1) + QLatin1Char('.'), m_showPage);
        auto* word = new QLabel(m_words[i], m_showPage);
        word->setTextInteractionFlags(Qt::TextSelectableByMouse);
        grid->addWidget(idx, i / 3, (i % 3) * 2);
        grid->addWidget(word, i / 3, (i % 3) * 2 + 1);
    }
    showLayout->addLayout(grid);

    auto* warn = new QLabel(
        QStringLiteral("Never share these words. Anyone with them can spend your funds."),
        m_showPage);
    warn->setWordWrap(true);
    showLayout->addWidget(warn);

    auto* nextBtn = new QPushButton(QStringLiteral("I wrote them down — continue"), m_showPage);
    showLayout->addWidget(nextBtn);

    m_confirmPage = new QWidget(stack);
    buildConfirmStep();

    stack->addWidget(m_showPage);
    stack->addWidget(m_confirmPage);
    root->addWidget(stack);

    connect(nextBtn, &QPushButton::clicked, this, [this, stack]() {
        stack->setCurrentWidget(m_confirmPage);
    });
}

MnemonicBackupDialog::~MnemonicBackupDialog()
{
    wipeSensitive();
}

void MnemonicBackupDialog::buildConfirmStep()
{
    auto* layout = new QVBoxLayout(m_confirmPage);
    auto* intro = new QLabel(
        QStringLiteral("Confirm a few words from your backup to continue."), m_confirmPage);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    const int n = m_words.size();
    const int ask = std::min(3, n);
    m_askIndices.clear();
    while (static_cast<int>(m_askIndices.size()) < ask) {
        const int idx = static_cast<int>(QRandomGenerator::global()->bounded(n));
        if (!m_askIndices.contains(idx)) {
            m_askIndices.push_back(idx);
        }
    }
    std::sort(m_askIndices.begin(), m_askIndices.end());

    m_confirmEdits.clear();
    for (int idx : m_askIndices) {
        auto* row = new QHBoxLayout;
        auto* lab = new QLabel(QStringLiteral("Word #%1").arg(idx + 1), m_confirmPage);
        auto* edit = new QLineEdit(m_confirmPage);
        edit->setPlaceholderText(QStringLiteral("type word"));
        row->addWidget(lab);
        row->addWidget(edit, 1);
        layout->addLayout(row);
        m_confirmEdits.push_back(edit);
    }

    auto* btnRow = new QHBoxLayout;
    auto* backBtn = new QPushButton(QStringLiteral("Back"), m_confirmPage);
    auto* okBtn = new QPushButton(QStringLiteral("Confirm backup"), m_confirmPage);
    okBtn->setDefault(true);
    btnRow->addWidget(backBtn);
    btnRow->addStretch(1);
    btnRow->addWidget(okBtn);
    layout->addLayout(btnRow);

    connect(backBtn, &QPushButton::clicked, this, [this]() {
        if (auto* stack = qobject_cast<QStackedWidget*>(m_showPage->parentWidget())) {
            stack->setCurrentWidget(m_showPage);
        }
    });
    connect(okBtn, &QPushButton::clicked, this, [this]() {
        for (int i = 0; i < m_askIndices.size(); ++i) {
            const QString got = m_confirmEdits[i]->text().trimmed().toLower();
            if (got != m_words[m_askIndices[i]]) {
                QMessageBox::warning(this, QStringLiteral("Mismatch"),
                                     QStringLiteral("Word #%1 does not match. Check your backup.")
                                         .arg(m_askIndices[i] + 1));
                return;
            }
        }
        wipeSensitive();
        accept();
    });
}

void MnemonicBackupDialog::wipeSensitive()
{
    for (QString& w : m_words) {
        wipeQString(w);
    }
    m_words.clear();
    for (QLineEdit* e : m_confirmEdits) {
        if (!e) continue;
        QString t = e->text();
        wipeQString(t);
        e->clear();
    }
}
