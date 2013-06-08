 php extension for webp<br/>
 create a webp img with php

INSTALL
====

    phpize
    ./configure --with-php-config=/usr/bin/php-config
    make
    sudo make install

php code
====

    <?php
    error_reporting(E_ALL);
    $file = "/home/www/test/webp/151933.jpg";
    $quality = 70 //default is 75
    image2webp($file, $file.".webp", $quality);
    ?>
    
links
====

    https://code.google.com/p/webp/issues/list
    https://developers.google.com/speed/webp/docs
    https://developers.google.com/speed/webp/docs/api
