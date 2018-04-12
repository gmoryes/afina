package tests::GetSetTest;

use strict;
use warnings;
use feature qw(say);

use FindBin;
use lib "$FindBin::Bin/lib";

use Server;
use Utils qw(quote_symbols);

sub main {
    my $self = shift;

    my $server = Server->new(port => 8080);
    return 0 unless $server;

    my $key = "key";
    my $value = "value";

    my $data = $server->set($key, $value);
    if ($data ne "STORED\r\n") {
        say "can not stored";
        return 0;
    }

    $data = $server->get($key);
    $server->close;
    
    my $must_be = "VALUE $key 0 ".length($value)."\r\n$value\r\nEND\r\n";
    if ($data eq $must_be) {
        return 1;
    } else {
        say "Error\nGet: " . quote_symbols($data) . "\nExpected: " . quote_symbols($must_be);
        return 0;
    }

}

1;

