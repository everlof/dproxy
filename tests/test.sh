#!/bin/bash

# http://httpbin.org/ip

direct_output=`curl -s http://httpbin.org/ip`
proxy_output=`curl -s --proxy1.0 127.0.0.1:49101 http://httpbin.org/ip`

diff <(echo -e "$direct_output") <(echo -e "$proxy_output")
result=$?

if [ $result -ne 0 ] ; then
    echo -e "NON-PROXY OUTPUT: \n>>>\n${direct_output}\n<<<\n"
    echo -e "    PROXY OUTPUT: \n>>>\n${proxy_output}\n<<<\n"
fi

echo "DIFF => $?"
