#pragma once

#include <QDialog>
#include <QString>
#include <QStringList>

class QLabel;
class QLineEdit;
class QPushButton;

// Show-once mnemonic backup: display words, then confirm random positions.
class MnemonicBackupDialog final : public QDialog {
  Q_OBJECT
 public:
  explicit MnemonicBackupDialog(const QString& mnemonic, QWidget* parent = nullptr);
  ~MnemonicBackupDialog() override;

 private:
  void buildConfirmStep();
  void wipeSensitive();

  QStringList m_words;
  QList<int> m_askIndices;
  QList<QLineEdit*> m_confirmEdits;
  QWidget* m_showPage = nullptr;
  QWidget* m_confirmPage = nullptr;
};
