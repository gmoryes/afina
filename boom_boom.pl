#!/usr/bin/perl

use warnings;
use strict;
use feature qw(say);
use DDP;

use Fcntl;
use IO::Socket::INET;

my $fork_cnt = 0;
say "Write forks num";
$fork_cnt = <>;
$fork_cnt = int($fork_cnt);

my $worker_num = 0;
my @pids = ();
my $logg_prefix = "worker";

sub logg {
    my @msg = @_;
    say "$logg_prefix [$worker_num, $$]".join(" ", @msg);
}

for my $i (1 .. $fork_cnt) {
    $worker_num++;
    if (my $pid = fork) {
        push @pids, $pid;
        next;
    }
    my $pre = $$;
    my $suf = 0;
    while (1) {
        logg "create socket";
        my $socket = get_socket_to_server(8080);
        unless ($socket) {
            logg "can not create socket";
            next;
        }
        $socket->autoflush(1);
        for (1..3) {
            my $msg = $pre."aaaaaaaaaaaaaaaaaaaaaaaaaafoo".$suf;
            $suf++;
            my $full_msg = "set $msg 0 0 6\r\nnewval\r\n";
            logg "start send($msg), size=".length($full_msg);
            socket_write($socket, $full_msg);

            my $data;
            recv($socket, $data, 2000, 0);
            logg "data = $data";
            $data = "";
        }
        close($socket);
    }
    exit;
}

say "wait until type";
sleep 120;
for my $pid (@pids) {
    kill 'INT', $pid;
}

sub quote_symbols {
    my ($msg) = @_;
    $msg =~ s/\n/\\n/sgm;
    $msg =~ s/\r/\\r/sgm;
    return $msg;
}

sub socket_write {
	my ($socket, $msg) = @_;
    my $sended = 0;
    my $buff;
    my $ret = recv($socket, $buff, 1, MSG_PEEK | MSG_DONTWAIT);;
    if (defined $ret) {
        return 0;
    }
    say "start send: \"".quote_symbols($msg)."\", size=".length($msg);
	$sended = send($socket, $msg, MSG_DONTWAIT | O_NONBLOCK);
    if ($sended) { 
        return 1;
    } else {
        return 0;
    }
}

sub get_socket_to_server {
	my ($port) = @_;
	my $socket = IO::Socket::INET->new(
		PeerAddr => 'localhost',
		PeerPort => $port,
		Proto => "tcp",
		Type => SOCK_STREAM,
	) or undef;

	return $socket;
}

1;
