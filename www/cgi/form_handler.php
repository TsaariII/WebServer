#!/usr/bin/php
<?php
parse_str(file_get_contents("php://input"), $post_vars);

echo "Content-Type: text/html\r\n\r\n";
echo "<html><body>";
echo "<h2>Form Data Received:</h2>";
echo "<pre>";
print_r($post_vars);
echo "</pre>";
echo "</body></html>";
?>