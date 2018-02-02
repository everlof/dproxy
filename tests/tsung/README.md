# Tsung test

This folder contains config files for `tsung` which can be used to test the proxy server.

## How to run

You start of the testing with:

```
$ tsung -k -f tsung.xml start
Starting Tsung
Log directory is: /Users/daveve/.tsung/log/20180202-0538
```

During the testing, and until you've manually terminated the `tsung` instance, you can view the status of the testing at `http://127.0.0.1:8091/`

### Generate statistics

After the testing is completed, to generate a report about the statistics of the test-run, you do:

```
cd /Users/daveve/.tsung/log/20180202-0538
/usr/local//Cellar/tsung/1.7.0/lib/tsung/bin/tsung_stats.pl
open report.html
```

## How to install tsung on macOS

Install with the following commands:

```
brew install tsung
sudo cpan Template
```