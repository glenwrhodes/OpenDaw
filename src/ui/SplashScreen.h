#pragma once

#include <QWidget>
#include <QPixmap>
#include <QString>

namespace freedaw {

class SplashScreen : public QWidget {
    Q_OBJECT

public:
    explicit SplashScreen(bool deleteOnClose = true, QWidget* parent = nullptr);

    void finish();

signals:
    void dismissed();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    QPixmap background_;
    bool    ready_ = false;
};

} // namespace freedaw
