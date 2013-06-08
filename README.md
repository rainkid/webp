  php extension for webp
  create a webp img with php

INSTALL
====

    phpize
    ./configure --with-php-config=/usr/bin/php-config
    make
    sudo make install

Code
====

    <?php
    error_reporting(E_ALL);
    $file = "/home/www/test/webp/151933.jpg";
    image2webp($file, $file.".webp");
    ?>
    

