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
    cmd = ['appium', '-p', str(aport), '-bp', str(bport),
           #'-U', args.uid,
           '--log-level', 'warn']
    proc = subprocess.Popen(cmd)
    print 'server', aport, 'started at pid', proc.pid
    return proc


def run_client(args, aport, url):
    driver_url = 'http://localhost:%s/wd/hub' % aport
    caps = {
        'platformName': 'Android',
        'platformVersion': args.version,
        'deviceName': args.uid,
        'udid': args.uid,
        'autoLaunch': 'true',
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

    buttons = [
        'new UiSelector().enabled(true).text("支付")',
        'new UiSelector().enabled(true).textContains("零钱支付")',
        'new UiSelector().textContains("完成")',
        ]
    error1 = 'new UiSelector().text("本次交易不支持使用零钱")'
    try:
        while 1:
            for btn in buttons:
                time.sleep(.1)
                el = find(btn)
                if not el:
                    continue
                el.click()
                sys.stdout.write('.')
                sys.stdout.flush()
            if find(error1):
                # http://developer.android.com/reference/android/view/KeyEvent.html
                driver.press_keycode(4)  # KEYCODE_BACK
                sys.stdout.write('B')
                sys.stdout.flush()
                time.sleep(.1)
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
