# coding: utf8
import sys
import time
import socket
import argparse
import subprocess

from appium import webdriver
from selenium.common.exceptions import NoSuchElementException


def parse_args():
    p = argparse.ArgumentParser()
    p.add_argument('uid')
    p.add_argument('url')
    p.add_argument('--version', default='4.4')
    return p.parse_args()


def free_port():
    s = socket.socket()
    s.bind(('', 0))
    p = s.getsockname()[1]
    s.close()
    return p


def wait_port(port, t=1):
    while 1:
        s = socket.socket()
        try:
            r = s.connect_ex(('127.0.0.1', port))
        finally:
            s.close()
        if r == 0:
            return
        sys.stdout.write('.')
        sys.stdout.flush()
        time.sleep(t)


def run_server(args, aport, bport):
    cmd = ['appium', '--udid', args.uid,
           '-p', str(aport), '-bp', str(bport),
           '--log-level', 'warn']
    proc = subprocess.Popen(cmd)
    print 'server', aport, 'started at pid', proc.pid
    return proc


def echo(ch):
    sys.stdout.write(ch)
    sys.stdout.flush()


def safe_click(el):
    """
     0: not exist
     1: click ok
    -1: error
    """
    if not el:
        return 0
    try:
        el.click()
    except NoSuchElementException:
        return -1
    return 1


def run_client(args, aport, url):
    driver_url = 'http://localhost:%s/wd/hub' % aport
    caps = {
        'platformName': 'Android',
        'platformVersion': args.version,
        'deviceName': args.uid,
        'udid': args.uid,
        'autoLaunch': True,
        'appPackage': 'com.tencent.movieticket',
        'appActivity': 'com.tencent.movieticket.activity.QQMovieTicketActivity',
    }
    driver = webdriver.Remote(driver_url, caps)

    def wait(xpath, t=.1):
        while 1:
            try:
                return driver.find_element_by_xpath(xpath)
            except NoSuchElementException:
                time.sleep(t)

    el = wait("//android.widget.EditText")
    print url
    el.set_text(url)

    def find(selector):
        try:
            return driver.find_element_by_android_uiautomator(selector)
        except NoSuchElementException:
            return

    def click_back():
        # http://developer.android.com/reference/android/view/KeyEvent.html
        driver.press_keycode(4)  # KEYCODE_BACK

    error1 = 'new UiSelector().text("本次交易不支持使用零钱")'
    error2 = 'new UiSelector().textContains("稍后再试")'  # 系统繁忙，请稍后再试
    error3 = 'new UiSelector().text("卡号")'
    error4 = 'new UiSelector().textContains("是否要放弃本次交易")'
    confirm = 'new UiSelector().text("确定")'
    yes = 'new UiSelector().text("是")'

    def handle_errors():
        if find(error1) or find(error3):
            click_back()
            echo('B')
            time.sleep(1)
        elif find(error2):
            el = find(confirm)
            if safe_click(el) == 1:
                echo('S')
                time.sleep(10)
        elif find(error4):
            el = find(yes)
            if safe_click(el) == 1:
                echo('A')
                time.sleep(1)

    buttons = [
        'new UiSelector().enabled(true).text("支付")',
        'new UiSelector().enabled(true).textContains("零钱支付")',
        'new UiSelector().textContains("完成")',
        ]
    try:
        while 1:
            for btn in buttons:
                time.sleep(.1)
                el = find(btn)
                if safe_click(el) == 1:
                    echo('.')
            handle_errors()
    finally:
        driver.quit()


def main():
    args = parse_args()
    aport = free_port()
    bport = free_port()

    proc = run_server(args, aport, bport)
    try:
        wait_port(aport)
        time.sleep(5)
        run_client(args, aport, args.url)
    finally:
        print 'try to kill server', aport
        proc.kill()
        print proc.pid, 'was killed'


if __name__ == '__main__':
    main()
