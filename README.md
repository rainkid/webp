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
    image2webp($file, $file.".webp");
    ?>
    

