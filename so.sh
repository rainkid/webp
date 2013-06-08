phpize
./configure --with-php-config=/usr/bin/php-config
make
sudo make install
sudo /etc/init.d/php5-fpm restart
