#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QString>
#include <QByteArray>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    mask = "Text Files (*.txt);;All Files (*)";


}

MainWindow::~MainWindow()
{
    delete ui;
}


void MainWindow::on_pushButton_clicked()
{

    filepath = QFileDialog::getOpenFileName(this, "Choose a file", "", mask);

}

void MainWindow::on_pushButton_2_clicked()
{
    QString input, perem;

    QFile file(filepath);
    file.open(QIODevice::ReadOnly | QIODevice::Text);
    QTextStream in(&file);
    input = in.readAll();
    file.close();
    perem = ui->lineEdit_perem->text();
    QByteArray inputByte = input.toUtf8();
    QByteArray peremByte = perem.toUtf8();
    QByteArray rezByte;

    for(int i = 0, j = 0; i < inputByte.size();++i, ++j)
    {
        if(j == peremByte.size())
            j = 0;
        rezByte.append(inputByte[i]^peremByte[j]);

    }
    file.setFileName(ui->lineEdit_save->text());
    file.write(rezByte);
    file.close();




}
