#ifndef QTHELPERS_H
#define QTHELPERS_H

// qt
#include <QString>
#include <QFile>
#include <QFileDialog>
#include <QDesktopServices>
#include <QCoreApplication>
#include <QLabel>
#include <QPushButton>
#include <QFont>
#include <QComboBox>
#include <QLineEdit>
#include <QTreeWidget>
#include <QPlainTextEdit>
#include <QHeaderView>
#include <QCheckBox>
#include <QProgressBar>
#include <QMdiArea>
#include <QMessageBox>

#include <exception>

#include <XboxInternals/Stfs/XContentHeader.h>
#include <QStandardPaths>
#include <QStringList>

#include <vector>

// other
#include <XboxInternals/Types.h>
#include <XboxInternals/Stfs/StfsConstants.h>
#include <ctype.h>

#ifdef _WIN32
#include <direct.h>
#endif

enum VelocityDropAction
{
    OpenInPackageViewer,
    RehashAndResign,
    OpenInProfileEditor
};

class QtHelpers
{
public:
    static QString ByteArrayToString(u8 *buffer, u32 len, bool spacesBetween);

    static QString ByteArrayToHexQString(u8 *buffer, u32 len);

    static u32 ParseHexString(QString string);

    static void ParseHexStringBuffer(QString bytes, u8 *outBuffer, int len);

    static QString DefaultLocation();

    // Report an exception that escaped C++ code into a UI handler. Backend
    // code throws std::string, but containers throw std::exception types
    // (std::out_of_range and friends) that a std::string handler misses.
    static void ShowUnexpectedError(QWidget *parent, const std::exception &error);

    static bool VerifyHexString(QString str);

    static bool VerifyDecimalString(QString str);

    static bool VerifyHexStringBuffer(QString bytes);

    static std::string GetKVPath(ConsoleType type, QWidget *parent = 0);

    // Asks for a keyvault and runs `resign` with it. Warns when nothing is
    // selected and shows an error when resigning fails, so a backend
    // std::string can never escape into the Qt event loop.
    template <typename ResignFn>
    static bool ResignWithKV(ConsoleType type, QWidget *parent, ResignFn &&resign)
    {
        const std::string kvPath = GetKVPath(type, parent);
        if (kvPath.empty())
        {
            QMessageBox::warning(parent, QObject::tr("No KeyVault"),
                    QObject::tr("No keyvault was selected, so the package was not resigned.\n\n"
                                "A console may refuse to load it."));
            return false;
        }

        try
        {
            resign(kvPath);
            return true;
        }
        catch (const std::string &error)
        {
            QMessageBox::critical(parent, QObject::tr("Resign Failed"),
                    QObject::tr("The package could not be resigned.\n\n%1")
                            .arg(QString::fromStdString(error)));
        }
        catch (const std::exception &error)
        {
            QMessageBox::critical(parent, QObject::tr("Resign Failed"),
                    QObject::tr("The package could not be resigned.\n\n%1")
                            .arg(QString::fromUtf8(error.what())));
        }

        return false;
    }

    static bool ParseVersionString(QString version, Version *out);

    static QString ExecutingDirectory();

    static QStringList StdStringArrayToQStringList(std::vector<std::string> strings);

    static void GenAdjustWidgetAppearanceToOS(QWidget *rootWidget);

    static void SearchTreeWidget(QTreeWidget *widget, QLineEdit *searchWidget, QString searchString);

    static void HideAllItems(QTreeWidgetItem *parent);

    static void ShowAllItems(QTreeWidgetItem *parent);

    static void CollapseAllChildren(QTreeWidgetItem *item);

    static void GetFileIcon(u32 magic, QString fileName, QIcon &icon, QTreeWidgetItem &item,
                            FileSystem fileSystem = FileSystemSTFS);

    static void AddSubWindow(QMdiArea *mdiArea, QWidget *widget);

    static void SetErrorStyle(QWidget *widget);

    static void ClearErrorStyle(QWidget *widget);

private:
    class SubWindowEvents : public QObject
    {
    public:
        SubWindowEvents(QObject* parent);

    protected:
        bool eventFilter(QObject *obj, QEvent *event);
    };
};

#endif // QTHELPERS_H


