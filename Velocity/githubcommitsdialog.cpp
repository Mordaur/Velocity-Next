#include "githubcommitsdialog.h"
#include "ui_githubcommitsdialog.h"

GitHubCommitsDialog::GitHubCommitsDialog(QWidget *parent) :
    QDialog(parent), ui(new Ui::GitHubCommitsDialog), branchCount(0), retrievedCount(0)
{
    // set the fixed size based on the OS
#ifdef _WIN32
    QSize appSize(274, 315);
#elif __unix__ | __APPLE__
    QSize appSize(345, 380);
#endif

    //setWindowFlags(windowFlags() | Qt::MSWindowsFixedSizeDialogHint);
    setMinimumSize(appSize);

    ui->setupUi(this);

    // create the objects needed
    commitsManager = new QNetworkAccessManager(this);
    branchesManager = new QNetworkAccessManager(this);
    label = new QLabel(this);

    // set the scroll area's widget to the label, so the user can scroll through the label's text
    ui->scrollArea->setWidget(label);

    label->setWordWrap(true);

    // make sure we can connect to the internet
    if (QNetworkInformation::instance()->reachability() != QNetworkInformation::Reachability::Online)
    {
        label->setText("<center>Error connecting to GitHub...</center>");
        return;
    }

    // make a request to the API
    connect(branchesManager, SIGNAL(finished(QNetworkReply*)), this,
            SLOT(onBrachesReply(QNetworkReply*)));
    connect(commitsManager, SIGNAL(finished(QNetworkReply*)), this,
            SLOT(onCommitsReply(QNetworkReply*)));
    
    // Create request with timeout
    QNetworkRequest request(QUrl("https://api.github.com/repos/Mordaur/Velocity-Next/branches"));
    request.setTransferTimeout(10000); // 10 second timeout
    branchesManager->get(request);
}

GitHubCommitsDialog::~GitHubCommitsDialog()
{
    delete ui;
}

void GitHubCommitsDialog::onBrachesReply(QNetworkReply *reply)
{
    // Check for network errors first
    if (reply->error() != QNetworkReply::NoError)
    {
        QString errorMsg = "<center>Error connecting to GitHub<br/>";
        if (reply->error() == QNetworkReply::TimeoutError)
            errorMsg += "(Connection timeout)</center>";
        else if (reply->error() == QNetworkReply::HostNotFoundError)
            errorMsg += "(Host not found)</center>";
        else
            errorMsg += "(" + reply->errorString() + ")</center>";
        
        label->setText(errorMsg);
        return;
    }
    
    // Parse the response using Qt's built-in JSON functionality
    QByteArray jsonData = reply->readAll();
    QJsonParseError parseError;
    QJsonDocument jsonDoc = QJsonDocument::fromJson(jsonData, &parseError);

    if (parseError.error != QJsonParseError::NoError || !jsonDoc.isArray())
    {
        label->setText("<center>Error parsing GitHub response<br/>(Invalid JSON)</center>");
        return;
    }

    // Convert the JSON array to a QList<QVariant>
    QJsonArray jsonArray = jsonDoc.array();
    QList<QVariant> branches = jsonArray.toVariantList();

    // Iterate through all of the branches
    for (const QVariant &branch : branches)
    {
        QVariantMap map = branch.toMap();
        QString url = map.value("commit").toMap().value("url").toString();

        url = url.mid(0, url.lastIndexOf("/")) + "?per_page=20&sha=" + url.mid(url.lastIndexOf("/") + 1);

        // Send a network request to fetch commit details with timeout
        QNetworkRequest request{QUrl(url)};
        request.setTransferTimeout(10000); // 10 second timeout
        commitsManager->get(request);

        branchCount++;
    }
}

void GitHubCommitsDialog::onCommitsReply(QNetworkReply *reply)
{
    // Check for network errors first
    if (reply->error() != QNetworkReply::NoError)
    {
        // Don't overwrite previous error messages
        if (!label->text().contains("Error"))
        {
            QString errorMsg = "<center>Error fetching commits<br/>";
            if (reply->error() == QNetworkReply::TimeoutError)
                errorMsg += "(Connection timeout)</center>";
            else
                errorMsg += "(" + reply->errorString() + ")</center>";
            
            label->setText(errorMsg);
        }
        return;
    }
    
    // Parse the response using Qt's built-in JSON functionality
    QByteArray jsonData = reply->readAll();
    QJsonParseError parseError;
    QJsonDocument jsonDoc = QJsonDocument::fromJson(jsonData, &parseError);

    if (parseError.error != QJsonParseError::NoError || !jsonDoc.isArray())
    {
        if (!label->text().contains("Error"))
            label->setText("<center>Error parsing commit data<br/>(Invalid JSON)</center>");
        return;
    }

    // Convert the JSON array to a QList<QVariant>
    QJsonArray jsonArray = jsonDoc.array();
    QList<QVariant> commits = jsonArray.toVariantList();

    // Iterate through all of the commits
    for (const QVariant &commit : commits)
    {
        QVariantMap map = commit.toMap();
        Commit c;

        // Extract the necessary data
        c.author = map.value("committer").toMap().value("login").toString();
        c.message = map.value("commit").toMap().value("message").toString();
        QString date = map.value("commit").toMap().value("author").toMap().value("date").toString();

        c.timestamp = QDateTime::fromString(date, Qt::ISODate);

        allCommits.push_back(c);
    }

    // Check if all branches have been retrieved
    if (++retrievedCount == branchCount)
    {
        // Sort the commits by timestamp
        std::sort(allCommits.begin(), allCommits.end(), commitCompare);

        // Display up to 20 commits
        int iterations = (allCommits.size() > 20) ? 20 : allCommits.size();
        for (int i = 0; i < iterations; ++i)
        {
            // Update the label with commit details
            label->setText(
                label->text() + "<b>" + allCommits.at(i).author + "</b> - " +
                allCommits.at(i).timestamp.toString("MM/dd/yyyy") + "<br />" +
                allCommits.at(i).message + "<br /><br />"
                );
        }
    }
}

bool commitCompare(Commit a, Commit b)
{
    return a.timestamp > b.timestamp;
}


