<?php
$opts = getopt('f:');
$blob = file_get_contents("/home/lvbenwei/".$opts['f']);
$res = image2webp($blob);
$arr = explode('/',$opts['f']);
file_put_contents("/home/lvbenwei/".$arr[count($arr)-1].".webp",$res);
