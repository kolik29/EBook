import styles from './App.module.sass';
import { Box, Paper } from '@mui/material';
import ActionBar from './components/layout/ActionBar';
import SearchBar from './components/layout/SearchBar';
import SelectedBookPanel from './components/books/SelectedBookPanel';
import BookCatalog from './components/books/BookCatalog';
import clsx from 'clsx';

export default function App() {
    return (
        <Box sx={{ p: { xs: 0, md: 2 } }} className={clsx('height__100vh', styles.appShell)}>
            <Box className={clsx('display__grid', 'width__100', 'height__100', 'position__relative', styles.container)}>
                <Box className={clsx('display__none--xs')}>
                    <Paper>
                        <ActionBar />
                    </Paper>
                </Box>
                <Box className={clsx('display__none--xs')}>
                    <Paper>
                        <SearchBar />
                    </Paper>
                </Box>
                <Box>
                    <Paper className={clsx('height__100')}><SelectedBookPanel /></Paper>
                </Box>
                <Box className={clsx(styles.gridItem)}>
                    <BookCatalog />
                </Box>
                <Box sx={{ p: 2 }} className={clsx('display__none--md')}>
                    <Box className={clsx('position__relative', 'display__grid', styles.bottomBar)}>
                        <Paper><ActionBar /></Paper>
                        <Paper><SearchBar /></Paper>
                    </Box>
                </Box>
            </Box>
        </Box>
    );
}
